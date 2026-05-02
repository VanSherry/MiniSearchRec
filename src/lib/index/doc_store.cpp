// ============================================================
// MiniSearchRec - 文档存储（SQLite 封装）实现
// ============================================================

#include "lib/index/doc_store.h"
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace minisearchrec {

struct DocStore::Impl {
    sqlite3* db = nullptr;
};

DocStore::DocStore() : impl_(new Impl()) {}

DocStore::~DocStore() {
    Close();
}

bool DocStore::Open(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        std::cerr << "[DocStore] Failed to open database: "
                  << (impl_->db ? sqlite3_errmsg(impl_->db) : "unknown") << "\n";
        // sqlite3_open 失败时句柄可能非 null，必须关闭
        if (impl_->db) {
            sqlite3_close(impl_->db);
            impl_->db = nullptr;
        }
        return false;
    }

    // WAL 模式提升并发读写性能
    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // 创建文档表
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
    rc = sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[DocStore] Failed to create table: " << err_msg << "\n";
        sqlite3_free(err_msg);
        sqlite3_close(impl_->db);  // BUG-9 修复：建表失败也必须关闭句柄
        impl_->db = nullptr;
        return false;
    }

    return true;
}

void DocStore::Close() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

bool DocStore::PutDoc(const Document& doc) {
    if (!impl_->db) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO docs
        (doc_id, title, content, author, publish_time, category, tags,
         quality_score, click_count, like_count, content_length)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
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

bool DocStore::GetDoc(const std::string& doc_id, Document& doc) {
    if (!impl_->db) return false;

    const char* sql = R"(
        SELECT doc_id, title, content, author, publish_time, category, tags,
               quality_score, click_count, like_count, content_length
        FROM docs WHERE doc_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, doc_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        doc.set_doc_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        doc.set_title(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        doc.set_content(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        doc.set_author(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        doc.set_publish_time(sqlite3_column_int64(stmt, 4));
        doc.set_category(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));

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

bool DocStore::DeleteDoc(const std::string& doc_id) {
    if (!impl_->db) return false;

    const char* sql = "DELETE FROM docs WHERE doc_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, doc_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<std::string> DocStore::GetAllDocIds() {
    std::vector<std::string> ids;
    if (!impl_->db) return ids;

    const char* sql = "SELECT doc_id FROM docs";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return ids;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return ids;
}

int64_t DocStore::GetDocCount() {
    if (!impl_->db) return 0;

    const char* sql = "SELECT COUNT(*) FROM docs";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

} // namespace minisearchrec
