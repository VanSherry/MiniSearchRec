// ============================================================
// MiniSearchRec - 搜索词热度存储实现
// ============================================================

#include "lib/storage/query_stats_store.h"
#include "utils/logger.h"
#include <ctime>

namespace minisearchrec {

bool QueryStatsStore::Initialize(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("QueryStatsStore: failed to open db: {}", db_path);
        return false;
    }

    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS query_stats ("
        "  query     TEXT PRIMARY KEY,"
        "  freq      INTEGER DEFAULT 1,"
        "  last_time INTEGER,"
        "  source    TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_query_freq ON query_stats(freq DESC);";

    char* err = nullptr;
    rc = sqlite3_exec(db_, create_sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        LOG_ERROR("QueryStatsStore: create table failed: {}", err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }

    initialized_ = true;
    LOG_INFO("QueryStatsStore initialized: {}", db_path);
    return true;
}

void QueryStatsStore::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    initialized_ = false;
}

bool QueryStatsStore::IncrementQuery(const std::string& query, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql =
        "INSERT INTO query_stats(query, freq, last_time, source) VALUES(?, 1, ?, ?)"
        " ON CONFLICT(query) DO UPDATE SET freq = freq + 1, last_time = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    int64_t now = std::time(nullptr);
    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_text(stmt, 3, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool QueryStatsStore::BatchInsert(const std::vector<QueryStatItem>& items) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || items.empty()) return false;

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql =
        "INSERT OR IGNORE INTO query_stats(query, freq, last_time, source) VALUES(?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    for (const auto& item : items) {
        sqlite3_bind_text(stmt, 1, item.query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, item.freq);
        sqlite3_bind_int64(stmt, 3, item.last_time);
        sqlite3_bind_text(stmt, 4, item.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

std::vector<QueryStatItem> QueryStatsStore::GetByPrefix(const std::string& prefix, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<QueryStatItem> results;
    if (!db_ || prefix.empty()) return results;

    const char* sql =
        "SELECT query, freq, last_time, source FROM query_stats "
        "WHERE query LIKE ? ORDER BY freq DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string pattern = prefix + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueryStatItem item;
        item.query     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.freq      = sqlite3_column_int(stmt, 1);
        item.last_time = sqlite3_column_int64(stmt, 2);
        const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.source    = src ? src : "";
        results.push_back(item);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<QueryStatItem> QueryStatsStore::GetTopN(int n) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<QueryStatItem> results;
    if (!db_) return results;

    const char* sql =
        "SELECT query, freq, last_time, source FROM query_stats "
        "ORDER BY freq DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_int(stmt, 1, n);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueryStatItem item;
        item.query     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.freq      = sqlite3_column_int(stmt, 1);
        item.last_time = sqlite3_column_int64(stmt, 2);
        const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.source    = src ? src : "";
        results.push_back(item);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<QueryStatItem> QueryStatsStore::GetTopNBySource(const std::string& source, int n) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<QueryStatItem> results;
    if (!db_) return results;

    const char* sql =
        "SELECT query, freq, last_time, source FROM query_stats "
        "WHERE source = ? ORDER BY freq DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, n);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueryStatItem item;
        item.query     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.freq      = sqlite3_column_int(stmt, 1);
        item.last_time = sqlite3_column_int64(stmt, 2);
        const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.source    = src ? src : "";
        results.push_back(item);
    }

    sqlite3_finalize(stmt);
    return results;
}

int QueryStatsStore::GetCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM query_stats;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace minisearchrec
