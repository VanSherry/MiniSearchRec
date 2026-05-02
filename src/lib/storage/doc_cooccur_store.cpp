// ============================================================
// MiniSearchRec - 文档行为共现存储实现
// ============================================================

#include "lib/storage/doc_cooccur_store.h"
#include "utils/logger.h"
#include <ctime>

namespace minisearchrec {

bool DocCooccurStore::Initialize(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DocCooccurStore: failed to open db: {}", db_path);
        return false;
    }

    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS doc_cooccur ("
        "  src_doc_id TEXT,"
        "  dst_doc_id TEXT,"
        "  co_count   INTEGER DEFAULT 1,"
        "  last_time  INTEGER,"
        "  PRIMARY KEY (src_doc_id, dst_doc_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_cooccur_src ON doc_cooccur(src_doc_id, co_count DESC);";

    char* err = nullptr;
    rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DocCooccurStore: create table failed: {}", err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }

    initialized_ = true;
    LOG_INFO("DocCooccurStore initialized: {}", db_path);
    return true;
}

void DocCooccurStore::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    initialized_ = false;
}

bool DocCooccurStore::RecordCooccurrence(const std::string& src_doc_id,
                                          const std::string& dst_doc_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || src_doc_id.empty() || dst_doc_id.empty()) return false;
    if (src_doc_id == dst_doc_id) return false;

    const char* sql =
        "INSERT INTO doc_cooccur(src_doc_id, dst_doc_id, co_count, last_time) "
        "VALUES(?, ?, 1, ?) "
        "ON CONFLICT(src_doc_id, dst_doc_id) DO UPDATE SET "
        "co_count = co_count + 1, last_time = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    int64_t now = std::time(nullptr);
    sqlite3_bind_text(stmt, 1, src_doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dst_doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_int64(stmt, 4, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // 双向共现
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, dst_doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src_doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<CooccurItem> DocCooccurStore::GetTopCooccur(const std::string& src_doc_id, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CooccurItem> results;
    if (!db_ || src_doc_id.empty()) return results;

    const char* sql =
        "SELECT dst_doc_id, co_count, last_time FROM doc_cooccur "
        "WHERE src_doc_id = ? ORDER BY co_count DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, src_doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CooccurItem item;
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.dst_doc_id = p ? p : "";
        item.co_count   = sqlite3_column_int(stmt, 1);
        item.last_time  = sqlite3_column_int64(stmt, 2);
        results.push_back(item);
    }

    sqlite3_finalize(stmt);
    return results;
}

} // namespace minisearchrec
