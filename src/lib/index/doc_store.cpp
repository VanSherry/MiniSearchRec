// ============================================================
// MiniSearchRec - 文档存储（SQLite 封装）实现
//
// 线程安全策略：
//   - 读操作：每个线程通过 thread_local 持有独立的 sqlite3* 连接，
//     SQLite WAL 模式天然支持多连接并发读，各线程互不干扰
//   - 写操作：使用独占写锁 + 专用写连接，同一时刻只有一个线程写
//   - Open/Close：非线程安全，仅在启动/关闭时调用
// ============================================================

#include "lib/index/doc_store.h"
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <shared_mutex>
#include <unordered_set>

namespace minisearchrec {

// ============================================================
// Impl：内部实现
// ============================================================
struct DocStore::Impl {
    // 数据库路径（用于 thread_local 连接懒创建）
    std::string db_path;

    // 专用写连接（仅写操作使用，由 write_mutex_ 独占保护）
    sqlite3* write_db = nullptr;

    // 写操作独占锁
    std::shared_mutex write_mutex;

    // 已打开标志
    bool opened = false;

    // thread_local 读连接的管理：通过 GetReadConn() 获取
    // 每个线程首次调用时自动创建，线程退出时自动销毁
};

namespace {

// ----------------------------------------------------------
// 打开一个 SQLite 连接并设置 WAL 模式
// ----------------------------------------------------------
sqlite3* OpenSqliteConn(const std::string& db_path) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "[DocStore] Failed to open read connection: "
                  << (db ? sqlite3_errmsg(db) : "unknown") << "\n";
        if (db) { sqlite3_close(db); }
        return nullptr;
    }
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    return db;
}

// ----------------------------------------------------------
// 关闭 thread_local 读连接的清理器
// ----------------------------------------------------------
struct ThreadConnCleaner {
    sqlite3* conn = nullptr;
    ~ThreadConnCleaner() {
        if (conn) {
            sqlite3_close(conn);
            conn = nullptr;
        }
    }
};

} // anonymous namespace

// ============================================================
// 构造 / 析构
// ============================================================
DocStore::DocStore() : impl_(new Impl()) {}

DocStore::~DocStore() {
    Close();
}

// ============================================================
// Open / Close
// ============================================================
bool DocStore::Open(const std::string& db_path) {
    impl_->db_path = db_path;

    // 打开写连接
    impl_->write_db = OpenSqliteConn(db_path);
    if (!impl_->write_db) return false;

    // 建表（通过写连接执行）
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS docs (
            doc_id TEXT PRIMARY KEY,
            title TEXT,
            content TEXT,
            author TEXT,
            publish_time INTEGER,
            category TEXT,
            tags TEXT,
            quality_score REAL,
            click_count INTEGER,
            like_count INTEGER,
            content_length INTEGER
        )
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(impl_->write_db, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[DocStore] Failed to create table: " << err_msg << "\n";
        sqlite3_free(err_msg);
        sqlite3_close(impl_->write_db);
        impl_->write_db = nullptr;
        return false;
    }

    impl_->opened = true;
    return true;
}

void DocStore::Close() {
    impl_->opened = false;

    // 关闭写连接
    if (impl_->write_db) {
        sqlite3_close(impl_->write_db);
        impl_->write_db = nullptr;
    }
    impl_->db_path.clear();

    // 注意：thread_local 读连接由各线程的 ThreadConnCleaner
    // 在线程退出时自动关闭，无需在此手动管理
}

// ============================================================
// 读操作：通过 thread_local 连接实现无锁并发读
// ============================================================

// 获取当前线程的读连接（懒创建）
sqlite3* DocStore::GetReadConn() {
    if (!impl_->opened || impl_->db_path.empty()) return nullptr;

    // 每个 thread_local 连接配套一个 cleaner，线程退出时自动关闭
    thread_local ThreadConnCleaner cleaner;
    if (!cleaner.conn) {
        cleaner.conn = OpenSqliteConn(impl_->db_path);
    }
    return cleaner.conn;
}

bool DocStore::GetDoc(const std::string& doc_id, Document& doc) {
    // 加读锁：防止读时正在写表结构等极端情况，
    // 同时保证 impl_->opened / db_path 的可见性
    std::shared_lock<std::shared_mutex> rlk(impl_->write_mutex);

    sqlite3* db = GetReadConn();
    if (!db) return false;

    const char* sql = R"(
        SELECT doc_id, title, content, author, publish_time, category, tags,
               quality_score, click_count, like_count, content_length
        FROM docs WHERE doc_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, doc_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto safe_text = [&](int col) -> std::string {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return p ? p : "";
        };
        doc.set_doc_id(safe_text(0));
        doc.set_title(safe_text(1));
        doc.set_content(safe_text(2));
        doc.set_author(safe_text(3));
        doc.set_publish_time(sqlite3_column_int64(stmt, 4));
        doc.set_category(safe_text(5));

        // 解析 tags
        const char* tags_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (tags_str) {
            std::string s(tags_str);
            std::stringstream ss(s);
            std::string tag;
            while (std::getline(ss, tag, ',')) {
                if (!tag.empty()) doc.add_tags(tag);
            }
        }

        doc.set_quality_score(sqlite3_column_double(stmt, 7));
        doc.set_click_count(sqlite3_column_int64(stmt, 8));
        doc.set_like_count(sqlite3_column_int64(stmt, 9));
        doc.set_content_length(sqlite3_column_int(stmt, 10));
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

std::vector<std::string> DocStore::GetAllDocIds() {
    std::shared_lock<std::shared_mutex> rlk(impl_->write_mutex);

    std::vector<std::string> ids;
    sqlite3* db = GetReadConn();
    if (!db) return ids;

    const char* sql = "SELECT doc_id FROM docs";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return ids;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (p) ids.push_back(p);
    }

    sqlite3_finalize(stmt);
    return ids;
}

int64_t DocStore::GetDocCount() {
    std::shared_lock<std::shared_mutex> rlk(impl_->write_mutex);

    sqlite3* db = GetReadConn();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM docs";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

// ============================================================
// 写操作：独占锁 + 专用写连接
// ============================================================

bool DocStore::PutDoc(const Document& doc) {
    std::unique_lock<std::shared_mutex> wlk(impl_->write_mutex);

    if (!impl_->write_db) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO docs
        (doc_id, title, content, author, publish_time, category, tags,
         quality_score, click_count, like_count, content_length)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->write_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, doc.doc_id().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, doc.title().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, doc.content().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, doc.author().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, doc.publish_time());
    sqlite3_bind_text(stmt, 6, doc.category().c_str(), -1, SQLITE_TRANSIENT);

    // tags 序列化为逗号分隔
    std::string tags_str;
    for (int i = 0; i < doc.tags_size(); ++i) {
        if (i > 0) tags_str += ",";
        tags_str += doc.tags(i);
    }
    sqlite3_bind_text(stmt, 7, tags_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, doc.quality_score());
    sqlite3_bind_int64(stmt, 9, doc.click_count());
    sqlite3_bind_int64(stmt, 10, doc.like_count());
    sqlite3_bind_int(stmt, 11, doc.content_length());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool DocStore::PutDocs(const std::vector<Document>& docs) {
    std::unique_lock<std::shared_mutex> wlk(impl_->write_mutex);

    if (!impl_->write_db) return false;

    // 开启事务批量写入
    sqlite3_exec(impl_->write_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = R"(
        INSERT OR REPLACE INTO docs
        (doc_id, title, content, author, publish_time, category, tags,
         quality_score, click_count, like_count, content_length)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->write_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(impl_->write_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    int ok_count = 0;
    for (const auto& doc : docs) {
        sqlite3_bind_text(stmt, 1, doc.doc_id().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, doc.title().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, doc.content().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, doc.author().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, doc.publish_time());
        sqlite3_bind_text(stmt, 6, doc.category().c_str(), -1, SQLITE_TRANSIENT);

        std::string tags_str;
        for (int i = 0; i < doc.tags_size(); ++i) {
            if (i > 0) tags_str += ",";
            tags_str += doc.tags(i);
        }
        sqlite3_bind_text(stmt, 7, tags_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 8, doc.quality_score());
        sqlite3_bind_int64(stmt, 9, doc.click_count());
        sqlite3_bind_int64(stmt, 10, doc.like_count());
        sqlite3_bind_int(stmt, 11, doc.content_length());

        rc = sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        if (rc == SQLITE_DONE) ++ok_count;
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(impl_->write_db, "COMMIT;", nullptr, nullptr, nullptr);

    return ok_count > 0;
}

bool DocStore::DeleteDoc(const std::string& doc_id) {
    std::unique_lock<std::shared_mutex> wlk(impl_->write_mutex);

    if (!impl_->write_db) return false;

    const char* sql = "DELETE FROM docs WHERE doc_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->write_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, doc_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

} // namespace minisearchrec
