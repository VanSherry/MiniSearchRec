// ============================================================
// MiniSearchRec - 用户行为上报接口处理器实现
// 负责：
//   1. 解析 HTTP 请求，提取 uid/doc_id/query/position/duration
//   2. 写入 SQLite search_events 表（用于离线训练样本导出）
//   3. 实时更新用户画像（UserProfileManager）
// ============================================================

#include "biz/event/event_handler.h"
#include "lib/user/user_profile.h"
#include "framework/app_context.h"
#include "lib/storage/doc_cooccur_store.h"
#include "lib/storage/query_stats_store.h"
#include "utils/logger.h"
#include <json/json.h>
#include <sqlite3.h>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace minisearchrec {

// ============================================================
// 事件日志数据库（单例，懒初始化）
// 写入 ./data/events.db，表 search_events
// ============================================================
namespace {

class EventDB {
public:
    static EventDB& Instance() {
        static EventDB inst;
        return inst;
    }

    // 初始化（幂等）
    bool Init(const std::string& db_path = "./data/events.db") {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (db_) return true;

        try { std::filesystem::create_directories("./data"); } catch (...) {}

        if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
            LOG_ERROR("EventDB: failed to open {}", db_path);
            db_ = nullptr;
            return false;
        }

        // WAL 模式提升并发写性能
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

        const char* ddl = R"(
            CREATE TABLE IF NOT EXISTS search_events (
                id            INTEGER PRIMARY KEY AUTOINCREMENT,
                uid           TEXT    NOT NULL DEFAULT '',
                doc_id        TEXT    NOT NULL,
                event_type    TEXT    NOT NULL,   -- click/like/share/dismiss
                query         TEXT    NOT NULL DEFAULT '',
                result_pos    INTEGER NOT NULL DEFAULT -1,  -- 结果排名位置（0-based）
                duration_ms   INTEGER NOT NULL DEFAULT 0,   -- 停留时长（毫秒）
                -- 展示时的特征快照（用于训练）
                bm25_score    REAL    NOT NULL DEFAULT 0,
                quality_score REAL    NOT NULL DEFAULT 0,
                freshness_score REAL  NOT NULL DEFAULT 0,
                coarse_score  REAL    NOT NULL DEFAULT 0,
                fine_score    REAL    NOT NULL DEFAULT 0,
                -- 文档静态特征
                click_count   INTEGER NOT NULL DEFAULT 0,
                like_count    INTEGER NOT NULL DEFAULT 0,
                doc_quality   REAL    NOT NULL DEFAULT 0,
                ts            INTEGER NOT NULL DEFAULT 0    -- Unix 秒
            )
        )";
        char* errmsg = nullptr;
        if (sqlite3_exec(db_, ddl, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            LOG_ERROR("EventDB: failed to create table: {}", errmsg ? errmsg : "");
            sqlite3_free(errmsg);
            return false;
        }

        // 查询常用索引
        sqlite3_exec(db_,
            "CREATE INDEX IF NOT EXISTS idx_events_uid ON search_events(uid);",
            nullptr, nullptr, nullptr);
        sqlite3_exec(db_,
            "CREATE INDEX IF NOT EXISTS idx_events_ts ON search_events(ts);",
            nullptr, nullptr, nullptr);

        LOG_INFO("EventDB: initialized at {}", db_path);
        return true;
    }

    // 写入一条事件
    bool WriteEvent(const std::string& uid,
                    const std::string& doc_id,
                    const std::string& event_type,
                    const std::string& query,
                    int result_pos,
                    int duration_ms,
                    float bm25_score,
                    float quality_score,
                    float freshness_score,
                    float coarse_score,
                    float fine_score,
                    int64_t click_count,
                    int64_t like_count,
                    float doc_quality,
                    int64_t ts) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!db_) {
            Init();
            if (!db_) return false;
        }

        const char* sql = R"(
            INSERT INTO search_events
            (uid, doc_id, event_type, query, result_pos, duration_ms,
             bm25_score, quality_score, freshness_score,
             coarse_score, fine_score,
             click_count, like_count, doc_quality, ts)
            VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text(stmt,  1, uid.c_str(),        -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,  2, doc_id.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,  3, event_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,  4, query.c_str(),      -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt,  5, result_pos);
        sqlite3_bind_int (stmt,  6, duration_ms);
        sqlite3_bind_double(stmt,7, bm25_score);
        sqlite3_bind_double(stmt,8, quality_score);
        sqlite3_bind_double(stmt,9, freshness_score);
        sqlite3_bind_double(stmt,10, coarse_score);
        sqlite3_bind_double(stmt,11, fine_score);
        sqlite3_bind_int64(stmt, 12, click_count);
        sqlite3_bind_int64(stmt, 13, like_count);
        sqlite3_bind_double(stmt,14, doc_quality);
        sqlite3_bind_int64(stmt, 15, ts);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    ~EventDB() {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

private:
    EventDB() = default;
    sqlite3* db_ = nullptr;
    std::recursive_mutex mu_;
};

} // anonymous namespace

// ============================================================
// EventHandler::Handle
// ============================================================
void EventHandler::Handle(const httplib::Request& req,
                           httplib::Response& res) {
    if (req.body.empty()) {
        res.set_content(R"({"ret":400,"err_msg":"Empty body"})", "application/json");
        res.status = 400;
        return;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(req.body);
    if (!Json::parseFromStream(builder, iss, &root, &errors)) {
        res.set_content(R"({"ret":400,"err_msg":"Invalid JSON"})", "application/json");
        res.status = 400;
        return;
    }

    // ---- 解析请求字段 ----
    std::string uid       = root.get("uid",         "").asString();
    std::string doc_id    = root.get("doc_id",       "").asString();
    std::string query     = root.get("query",        "").asString();
    int result_pos        = root.get("result_pos",   -1).asInt();
    int duration_ms       = root.get("duration_ms",   0).asInt();

    // 特征快照（前端可在曝光时随结果一起缓存，点击时上报）
    float bm25_score      = root.get("bm25_score",      0.0f).asFloat();
    float quality_score   = root.get("quality_score",   0.0f).asFloat();
    float freshness_score = root.get("freshness_score", 0.0f).asFloat();
    float coarse_score    = root.get("coarse_score",    0.0f).asFloat();
    float fine_score      = root.get("fine_score",      0.0f).asFloat();

    // 从路径推断事件类型
    std::string event_type = "unknown";
    if      (req.path.find("click")   != std::string::npos) event_type = "click";
    else if (req.path.find("like")    != std::string::npos) event_type = "like";
    else if (req.path.find("share")   != std::string::npos) event_type = "share";
    else if (req.path.find("dismiss") != std::string::npos) event_type = "dismiss";
    else if (req.path.find("dwell")   != std::string::npos) event_type = "dwell";

    int64_t ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    LOG_INFO("EventHandler: uid={}, event={}, doc_id={}, pos={}, ts={}",
             uid, event_type, doc_id, result_pos, ts);

    // ---- 1. 从 DocStore 补全文档静态特征 ----
    int64_t click_count = 0;
    int64_t like_count  = 0;
    float   doc_quality = 0.0f;

    auto doc_store = AppContext::Instance().GetDocStore();
    if (doc_store && !doc_id.empty()) {
        Document doc;
        if (doc_store->GetDoc(doc_id, doc)) {
            click_count = doc.click_count();
            like_count  = doc.like_count();
            doc_quality = doc.quality_score();
        }
    }

    // ---- 2. 写入事件日志（SQLite）----
    EventDB::Instance().Init();
    bool write_ok = EventDB::Instance().WriteEvent(
        uid, doc_id, event_type, query,
        result_pos, duration_ms,
        bm25_score, quality_score, freshness_score,
        coarse_score, fine_score,
        click_count, like_count, doc_quality,
        ts
    );
    if (!write_ok) {
        LOG_WARN("EventHandler: failed to write event to DB uid={} doc={}", uid, doc_id);
    }

    // ---- 3. 实时更新用户画像 ----
    if (!uid.empty() && !doc_id.empty() &&
        (event_type == "click" || event_type == "like")) {
        UserProfileManager mgr;
        if (!mgr.UpdateFromEvent(uid, doc_id, event_type)) {
            LOG_WARN("EventHandler: UpdateFromEvent failed uid={}", uid);
        }
    }

    // ---- 4. 写入搜索词频（供 Sug/Nav 使用）----
    if (!query.empty() && event_type == "click") {
        QueryStatsStore::Instance().IncrementQuery(query, "search_click");
    }

    // ---- 5. 写入文档行为共现（供 Hint 使用）----
    // 维护 per-uid 最近点击 doc_id，连续点击不同文档时记录共现
    if (!uid.empty() && !doc_id.empty() && event_type == "click") {
        static std::mutex s_last_click_mutex;
        static std::unordered_map<std::string, std::string> s_last_click_map;  // uid -> last_doc_id

        std::string last_doc_id;
        {
            std::lock_guard<std::mutex> lk(s_last_click_mutex);
            // 防止无界增长：超过 10000 条时清理
            if (s_last_click_map.size() > 10000) {
                s_last_click_map.clear();
            }
            auto it = s_last_click_map.find(uid);
            if (it != s_last_click_map.end()) {
                last_doc_id = it->second;
            }
            s_last_click_map[uid] = doc_id;
        }

        // 如果用户连续点击了两篇不同文档，记录共现
        if (!last_doc_id.empty() && last_doc_id != doc_id) {
            DocCooccurStore::Instance().RecordCooccurrence(last_doc_id, doc_id);
            LOG_INFO("EventHandler: recorded cooccurrence {}→{}", last_doc_id, doc_id);
        }
    }

    res.set_content(R"({"ret":0,"err_msg":""})", "application/json");
    res.status = 200;
}

} // namespace minisearchrec
