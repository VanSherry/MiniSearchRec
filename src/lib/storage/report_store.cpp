// ============================================================
// MiniSearchRec - 异步曝光/点击上报存储实现
// ============================================================

#include "lib/storage/report_store.h"
#include "utils/logger.h"
#include <sqlite3.h>
#include <chrono>
#include <ctime>
#include <json/json.h>
#include <sstream>

namespace minisearchrec {

bool ReportStore::Initialize(const std::string& db_path) {
    db_path_ = db_path;

    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        LOG_ERROR("ReportStore: failed to open db: {}", db_path);
        return false;
    }

    const char* ddl = R"(
        CREATE TABLE IF NOT EXISTS report_stats (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            ts         INTEGER NOT NULL,
            biz_type   TEXT NOT NULL,
            event_type TEXT NOT NULL,
            count      INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_report_ts ON report_stats(ts);
        CREATE INDEX IF NOT EXISTS idx_report_biz ON report_stats(biz_type, ts);
    )";

    char* err = nullptr;
    if (sqlite3_exec(db_, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        LOG_ERROR("ReportStore: create table failed: {}", err ? err : "");
        sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // WAL 模式提升并发
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    running_.store(true);
    worker_ = std::thread(&ReportStore::FlushLoop, this);

    LOG_INFO("ReportStore: initialized at {}", db_path);
    return true;
}

void ReportStore::Stop() {
    if (!running_.load()) return;
    stop_requested_.store(true);
    queue_cv_.notify_one();
    if (worker_.joinable()) worker_.join();
    running_.store(false);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
    LOG_INFO("ReportStore: stopped");
}

void ReportStore::Report(const std::string& biz_type,
                          const std::string& event_type,
                          int count) {
    if (!running_.load()) return;
    ReportEvent ev;
    ev.biz_type   = biz_type;
    ev.event_type = event_type;
    ev.count      = count;
    ev.ts         = std::time(nullptr);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(std::move(ev));
    }
    queue_cv_.notify_one();
}

void ReportStore::FlushLoop() {
    const int batch_size = 50;
    const int max_wait_ms = 1000;

    const char* sql = "INSERT INTO report_stats(ts, biz_type, event_type, count) "
                      "VALUES(?,?,?,?);";

    while (!stop_requested_.load()) {
        std::vector<ReportEvent> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (queue_.empty()) {
                queue_cv_.wait_for(lock, std::chrono::milliseconds(max_wait_ms),
                                   [this]() { return !queue_.empty() || stop_requested_.load(); });
            }
            // 批量取出
            while (!queue_.empty() && static_cast<int>(batch.size()) < batch_size) {
                batch.push_back(std::move(queue_.front()));
                queue_.pop();
            }
        }

        if (batch.empty()) continue;

        // 开启事务批量写入
        sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            for (const auto& ev : batch) {
                sqlite3_bind_int64(stmt, 1, ev.ts);
                sqlite3_bind_text(stmt, 2, ev.biz_type.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, ev.event_type.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 4, ev.count);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    }
}

std::string ReportStore::QueryTimeSeries(const std::string& biz_type,
                                          int hours,
                                          int bucket_sec) {
    if (!db_) {
        Json::Value root;
        root["ret"] = 500; root["err_msg"] = "ReportStore not initialized";
        Json::StreamWriterBuilder w; w["indentation"] = "";
        return Json::writeString(w, root);
    }

    int64_t since_ts = std::time(nullptr) - hours * 3600;

    // 使用子查询按 biz_type 分组统计
    std::string sql =
        "SELECT (ts / ?) * ? AS bucket_start, "
        "  SUM(CASE WHEN event_type='impression' THEN count ELSE 0 END) AS impressions, "
        "  SUM(CASE WHEN event_type='click' THEN count ELSE 0 END) AS clicks "
        "FROM report_stats WHERE ts >= ? AND biz_type = ? "
        "GROUP BY bucket_start ORDER BY bucket_start ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Json::Value root;
        root["ret"] = 500; root["err_msg"] = sqlite3_errmsg(db_);
        Json::StreamWriterBuilder w; w["indentation"] = "";
        return Json::writeString(w, root);
    }

    sqlite3_bind_int(stmt, 1, bucket_sec);
    sqlite3_bind_int(stmt, 2, bucket_sec);
    sqlite3_bind_int64(stmt, 3, since_ts);
    sqlite3_bind_text(stmt, 4, biz_type.c_str(), -1, SQLITE_STATIC);

    int64_t total_imp = 0, total_click = 0;
    Json::Value items(Json::arrayValue);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t ts       = sqlite3_column_int64(stmt, 0);
        int64_t imp      = sqlite3_column_int64(stmt, 1);
        int64_t click    = sqlite3_column_int64(stmt, 2);
        total_imp += imp;
        total_click += click;

        Json::Value j;
        j["ts"]          = static_cast<Json::Value::Int64>(ts);
        j["impression"]  = static_cast<Json::Value::Int64>(imp);
        j["click"]       = static_cast<Json::Value::Int64>(click);
        items.append(std::move(j));
    }
    sqlite3_finalize(stmt);

    Json::Value summary;
    summary["total_impressions"] = static_cast<Json::Value::Int64>(total_imp);
    summary["total_clicks"]      = static_cast<Json::Value::Int64>(total_click);
    summary["ctr"]               = total_imp > 0 ? (float)total_click / (float)total_imp : 0.0f;

    Json::Value data;
    data["biz"]        = biz_type;
    data["hours"]      = hours;
    data["bucket_sec"] = bucket_sec;
    data["summary"]    = std::move(summary);
    data["items"]      = std::move(items);

    Json::Value root;
    root["ret"]     = 0;
    root["err_msg"] = "";
    root["data"]    = std::move(data);

    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
}

} // namespace minisearchrec
