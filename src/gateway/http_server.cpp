// ============================================================
// MiniSearchRec - HTTP 服务器实现
// 使用 cpp-httplib（header-only）
// ============================================================

#include "gateway/http_server.h"
#include "gateway/admin_panel.h"
#include "framework/server/server.h"
#include "biz/doc/doc_handler.h"
#include "biz/event/event_handler.h"
#include "biz/search/search_handler.h"
#include "biz/sug/sug_handler.h"
#include "biz/sug/sug_trie.h"
#include "lib/storage/query_stats_store.h"
#include "lib/storage/doc_cooccur_store.h"
#include "lib/storage/report_store.h"
#include "scheduler/task/index_rebuild_task.h"
#include "scheduler/task/auto_train_task.h"
#include "framework/app_context.h"
#include "framework/config/config_manager.h"
#include "ab/ab_test.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <ctime>
#include <json/json.h>
#include <yaml-cpp/yaml.h>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

namespace minisearchrec {

HttpServer::HttpServer(const std::string& host, int port)
    : host_(host), port_(port) {
    server_ = std::make_unique<httplib::Server>();
}

HttpServer::~HttpServer() {
    Stop();
}

bool HttpServer::Initialize() {
    RegisterRoutes();
    std::cout << "[HttpServer] Routes registered.\n";
    return true;
}

void HttpServer::RegisterRoutes() {
    // 健康检查
    server_->Get("/health", [this](const auto& req, auto& res) {
        HandleHealthCheck(req, res);
    });

    // 管理后台页面（嵌入式 HTML）
    server_->Get("/admin", [](const auto& /*req*/, auto& res) {
        res.set_content(kAdminHtml, "text/html; charset=utf-8");
        res.status = 200;
    });

    // 搜索接口
    server_->Post("/api/v1/search", [this](const auto& req, auto& res) {
        HandleSearch(req, res);
    });

    // 文档管理接口
    server_->Post("/api/v1/doc/add", [this](const auto& req, auto& res) {
        HandleAddDoc(req, res);
    });
    server_->Put("/api/v1/doc/update", [this](const auto& req, auto& res) {
        HandleUpdateDoc(req, res);
    });
    server_->Delete("/api/v1/doc/delete", [this](const auto& req, auto& res) {
        HandleDeleteDoc(req, res);
    });
    server_->Get("/api/v1/doc/get", [this](const auto& req, auto& res) {
        HandleGetDoc(req, res);
    });
    server_->Get("/api/v1/doc/list", [this](const auto& req, auto& res) {
        HandleListDocs(req, res);
    });

    // 用户行为上报接口
    server_->Post("/api/v1/event/click", [this](const auto& req, auto& res) {
        HandleReportEvent(req, res);
    });
    server_->Post("/api/v1/event/like", [this](const auto& req, auto& res) {
        HandleReportEvent(req, res);
    });

    // Sug 搜索建议接口
    server_->Get("/api/v1/sug", [this](const auto& req, auto& res) {
        HandleSug(req, res);
    });

    // Hint 相关搜索（点后推）接口
    server_->Get("/api/v1/hint", [this](const auto& req, auto& res) {
        HandleHint(req, res);
    });

    // Nav 教育页（搜前引导）接口
    server_->Get("/api/v1/nav", [this](const auto& req, auto& res) {
        HandleNav(req, res);
    });

    // ── Admin 接口（内部数据查询，不走框架路由）──

    // 查询搜索词统计数据（无 prefix 返回 TopN 热词，有 prefix 返回前缀匹配）
    server_->Get("/api/v1/admin/stats/query", [](const auto& req, auto& res) {
        auto& store = QueryStatsStore::Instance();
        std::string prefix;
        if (req.has_param("prefix")) {
            prefix = req.get_param_value("prefix");
        }

        int top = 30;
        if (req.has_param("top")) {
            top = std::max(1, std::stoi(req.get_param_value("top")));
        }

        Json::Value items(Json::arrayValue);
        std::vector<QueryStatItem> result;
        if (prefix.empty()) {
            result = store.GetTopN(top);
        } else {
            result = store.GetByPrefix(prefix, top);
        }

        for (const auto& item : result) {
            Json::Value j;
            j["query"]     = item.query;
            j["freq"]      = item.freq;
            j["last_time"] = item.last_time;
            j["source"]    = item.source;
            items.append(std::move(j));
        }

        Json::Value data;
        data["total"] = static_cast<int>(items.size());
        data["items"] = std::move(items);

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 查询文档共现数据
    server_->Get("/api/v1/admin/stats/cooccur", [](const auto& req, auto& res) {
        std::string doc_id;
        if (!req.has_param("doc_id") || req.get_param_value("doc_id").empty()) {
            Json::Value root;
            root["ret"]     = 400;
            root["err_msg"] = "doc_id is required";
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            res.set_content(Json::writeString(w, root), "application/json");
            res.status = 400;
            return;
        }
        doc_id = req.get_param_value("doc_id");

        int top = 20;
        if (req.has_param("top")) {
            top = std::max(1, std::stoi(req.get_param_value("top")));
        }

        Json::Value items(Json::arrayValue);
        for (const auto& ci : DocCooccurStore::Instance().GetTopCooccur(doc_id, top)) {
            Json::Value j;
            j["dst_doc_id"] = ci.dst_doc_id;
            j["co_count"]   = ci.co_count;
            j["last_time"]  = ci.last_time;
            items.append(std::move(j));
        }

        Json::Value data;
        data["total"] = static_cast<int>(items.size());
        data["items"] = std::move(items);

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 查询 SugTrie 内容
    server_->Get("/api/v1/admin/sug/trie", [](const auto& req, auto& res) {
        std::string prefix;
        if (!req.has_param("prefix") || req.get_param_value("prefix").empty()) {
            Json::Value root;
            root["ret"]     = 400;
            root["err_msg"] = "prefix is required";
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            res.set_content(Json::writeString(w, root), "application/json");
            res.status = 400;
            return;
        }
        prefix = req.get_param_value("prefix");

        int limit = 50;
        if (req.has_param("limit")) {
            limit = std::max(1, std::stoi(req.get_param_value("limit")));
        }

        Json::Value items(Json::arrayValue);
        for (const auto* entry : SugTrie::Instance().Search(prefix, limit)) {
            Json::Value j;
            j["word"]          = entry->word;
            j["source"]        = entry->source;
            j["freq"]          = entry->freq;
            j["last_time"]     = entry->last_time;
            j["source_weight"] = entry->source_weight;
            items.append(std::move(j));
        }

        Json::Value data;
        data["total"] = static_cast<int>(items.size());
        data["items"] = std::move(items);

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 系统总览仪表盘
    server_->Get("/api/v1/admin/dashboard", [](const auto& /*req*/, auto& res) {
        auto& ctx = AppContext::Instance();
        auto inv = ctx.GetInvertedIndex();
        auto vec = ctx.GetVectorIndex();

        Json::Value data;
        data["doc_count"]        = inv ? static_cast<Json::Value::Int64>(inv->GetDocCount()) : 0;
        data["term_count"]       = inv ? static_cast<Json::Value::Int64>(inv->GetTermCount()) : 0;
        data["vector_count"]     = vec ? static_cast<Json::Value::Int64>(vec->GetVectorCount()) : 0;
        data["query_stats_count"]= QueryStatsStore::Instance().GetCount();
        data["cooccur_count"]    = DocCooccurStore::Instance().GetCount();
        data["sug_trie_size"]    = static_cast<Json::Value::Int64>(SugTrie::Instance().Size());
        data["ready"]            = ctx.IsReady();

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 触发索引全量重建
    server_->Post("/api/v1/admin/index/rebuild", [](const auto& /*req*/, auto& res) {
        scheduler::IndexRebuildConfig cfg;
        cfg.enable = true;
        scheduler::IndexRebuildTask task(cfg);
        bool ok = task.RebuildAtomically();

        Json::Value root;
        root["ret"]     = ok ? 0 : 500;
        root["err_msg"] = ok ? "" : "Index rebuild failed";
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = ok ? 200 : 500;
    });

    // 触发性 SugTrie 重建
    server_->Post("/api/v1/admin/sug/trie/rebuild", [](const auto& /*req*/, auto& res) {
        SugHandler::RebuildTrie();

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 查询用户事件数据
    server_->Get("/api/v1/admin/events", [](const auto& req, auto& res) {
        std::string uid = req.has_param("uid") ? req.get_param_value("uid") : "";
        std::string event_type = req.has_param("event_type") ? req.get_param_value("event_type") : "";
        int limit = 50;
        if (req.has_param("limit")) limit = std::max(1, std::min(200, std::stoi(req.get_param_value("limit"))));
        int64_t since_ts = req.has_param("since") ? std::stoll(req.get_param_value("since")) : 0;

        std::string json = EventHandler::QueryEventsAsJson(uid, event_type, limit, since_ts);
        res.set_content(json, "application/json");
        res.status = 200;
    });

    // 调度器任务状态
    server_->Get("/api/v1/admin/scheduler/status", [](const auto& /*req*/, auto& res) {
        auto* sched = scheduler::Scheduler::GetInstance();
        if (!sched) {
            Json::Value root;
            root["ret"]     = 500;
            root["err_msg"] = "Scheduler not available";
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            res.set_content(Json::writeString(w, root), "application/json");
            res.status = 500;
            return;
        }

        Json::Value items(Json::arrayValue);
        for (const auto& st : sched->GetAllTaskStatus()) {
            Json::Value j;
            j["name"]           = st.name;
            j["enabled"]        = st.enabled;
            j["interval_sec"]   = st.interval_sec;
            j["last_run_epoch"] = st.last_run_epoch;
            items.append(std::move(j));
        }

        Json::Value data;
        data["running"] = sched->IsRunning();
        data["total"]   = static_cast<int>(items.size());
        data["items"]   = std::move(items);

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // A/B 实验概览
    server_->Get("/api/v1/admin/abtest", [](const auto& req, auto& res) {
        auto ab_mgr = AppContext::Instance().GetABTestManager();
        if (!ab_mgr) {
            Json::Value root;
            root["ret"]     = 0;
            root["err_msg"] = "";
            root["data"]    = Json::Value(Json::objectValue);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            res.set_content(Json::writeString(w, root), "application/json");
            res.status = 200;
            return;
        }

        Json::Value items(Json::arrayValue);
        for (const auto& exp : ab_mgr->GetAllExperiments()) {
            Json::Value j;
            j["name"]          = exp.name;
            j["traffic_ratio"] = exp.traffic_ratio;
            j["bucket_method"] = exp.bucket_method;
            Json::Value params(Json::objectValue);
            for (const auto& [k, v] : exp.params) {
                params[k] = v;
            }
            j["params"] = std::move(params);
            items.append(std::move(j));
        }

        // 如果提供了 uid，显示该用户的分组
        std::string assign_to;
        if (req.has_param("uid") && !req.get_param_value("uid").empty()) {
            const auto* exp = ab_mgr->AssignExperiment(req.get_param_value("uid"));
            assign_to = exp ? exp->name : "control";
        }

        Json::Value data;
        data["total"]         = static_cast<int>(items.size());
        data["experiments"]   = std::move(items);
        if (!assign_to.empty()) {
            data["assigned_to"] = assign_to;
        }

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 时间序列统计（数据看板用）
    server_->Get("/api/v1/admin/stats/timeseries", [](const auto& req, auto& res) {
        int hours = 24;
        if (req.has_param("hours")) hours = std::max(1, std::min(720, std::stoi(req.get_param_value("hours"))));
        int bucket_sec = 3600; // 默认按小时
        if (req.has_param("bucket")) {
            int b = std::stoi(req.get_param_value("bucket"));
            if (b == 300 || b == 900 || b == 3600 || b == 86400) bucket_sec = b;
        }

        int64_t since_ts = std::time(nullptr) - hours * 3600;

        std::string data_dir = ConfigManager::Instance().GetGlobalConfig().index.data_dir;
        if (data_dir.empty()) data_dir = "./data/";
        if (data_dir.back() != '/') data_dir += '/';
        std::string db_path = data_dir + "events.db";

        sqlite3* db = nullptr;
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            Json::Value root; root["ret"]=500; root["err_msg"]="Failed to open events.db";
            Json::StreamWriterBuilder w; w["indentation"] = "";
            res.set_content(Json::writeString(w, root), "application/json"); res.status=500;
            return;
        }

        const char* sql =
            "SELECT (ts / ?) * ? AS bucket_start, "
            "  COUNT(*) AS total_events, "
            "  SUM(CASE WHEN event_type='click' THEN 1 ELSE 0 END) AS clicks, "
            "  SUM(CASE WHEN event_type='like' THEN 1 ELSE 0 END) AS likes "
            "FROM search_events WHERE ts >= ? "
            "GROUP BY bucket_start ORDER BY bucket_start ASC;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            Json::Value root; root["ret"]=500; root["err_msg"]=sqlite3_errmsg(db);
            Json::StreamWriterBuilder w; w["indentation"] = "";
            res.set_content(Json::writeString(w, root), "application/json"); res.status=500;
            return;
        }

        sqlite3_bind_int(stmt, 1, bucket_sec);
        sqlite3_bind_int(stmt, 2, bucket_sec);
        sqlite3_bind_int64(stmt, 3, since_ts);

        int64_t total_clicks = 0, total_events = 0;
        Json::Value items(Json::arrayValue);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t ts      = sqlite3_column_int64(stmt, 0);
            int64_t events  = sqlite3_column_int64(stmt, 1);
            int64_t clicks  = sqlite3_column_int64(stmt, 2);
            int64_t likes   = sqlite3_column_int64(stmt, 3);
            total_events += events;
            total_clicks += clicks;

            Json::Value j;
            j["ts"]    = ts;
            j["total"] = static_cast<Json::Value::Int64>(events);
            j["click"] = static_cast<Json::Value::Int64>(clicks);
            j["like"]  = static_cast<Json::Value::Int64>(likes);
            items.append(std::move(j));
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);

        Json::Value summary;
        summary["total_events"] = static_cast<Json::Value::Int64>(total_events);
        summary["total_clicks"] = static_cast<Json::Value::Int64>(total_clicks);
        summary["total_likes"]  = static_cast<Json::Value::Int64>(total_events - total_clicks);
        summary["ctr"]          = total_events > 0 ? (float)total_clicks / (float)total_events : 0.0f;

        Json::Value data;
        data["hours"]       = hours;
        data["bucket_sec"]  = bucket_sec;
        data["summary"]     = std::move(summary);
        data["items"]       = std::move(items);

        Json::Value root;
        root["ret"]     = 0;
        root["err_msg"] = "";
        root["data"]    = std::move(data);

        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // ReportStore 时间序列查询（4 biz 面板用）
    server_->Get("/api/v1/admin/report/timeseries", [](const auto& req, auto& res) {
        std::string biz = req.has_param("biz") ? req.get_param_value("biz") : "search";
        int hours = 24;
        if (req.has_param("hours")) hours = std::max(1, std::min(720, std::stoi(req.get_param_value("hours"))));
        int bucket_sec = 3600;
        if (req.has_param("bucket")) {
            int b = std::stoi(req.get_param_value("bucket"));
            if (b == 300 || b == 900 || b == 3600 || b == 86400) bucket_sec = b;
        }
        std::string json = ReportStore::Instance().QueryTimeSeries(biz, hours, bucket_sec);
        res.set_content(json, "application/json");
        res.status = 200;
    });

    // ── 模型管理接口（训练数据 Dump / 训练 / 热更新）──

    // 辅助：读取 AutoTrain 配置
    static auto ReadAutoTrainConfig = []() -> scheduler::AutoTrainConfig {
        scheduler::AutoTrainConfig cfg;
        std::string config_dir = ConfigManager::Instance().GetConfigDir();
        if (config_dir.empty()) config_dir = "./config";
        std::string yaml_path = config_dir + "/framework.yaml";
        try {
            auto root = YAML::LoadFile(yaml_path);
            auto node = root["background"]["auto_train"];
            if (node) {
                cfg.dump_tool         = node["dump_tool"].as<std::string>("./build/dump_train_data");
                cfg.train_script      = node["train_script"].as<std::string>("./scripts/train_rank_model.py");
                cfg.model_output      = node["model_output"].as<std::string>("./models/rank_model.txt");
                cfg.events_db         = node["events_db"].as<std::string>("./data/events.db");
                cfg.docs_db           = node["docs_db"].as<std::string>("./data/docs.db");
                cfg.train_data_output = node["train_data_output"].as<std::string>("./data/train.txt");
            }
        } catch (...) {}
        // 修正路径：config 中的路径基于项目根目录，但服务从 build/ 启动时需调整。
        // 检查路径是否存在，不存在则尝试加 ../ 前缀
        auto fix_path = [](std::string& p) {
            if (p.empty()) return;
            // 如果以 ./ 开头且文件不存在，尝试从项目根目录查找
            if (p.find("./") == 0) {
                std::ifstream f(p);
                if (!f.good()) {
                    std::string alt = "../" + p.substr(2);
                    std::ifstream f2(alt);
                    if (f2.good()) p = alt;
                } else { f.close(); }
            }
        };
        fix_path(cfg.dump_tool);
        fix_path(cfg.train_script);
        fix_path(cfg.model_output);
        fix_path(cfg.events_db);
        fix_path(cfg.docs_db);
        fix_path(cfg.train_data_output);
        return cfg;
    };

    // 模型状态查询
    server_->Get("/api/v1/admin/model/status", [](const auto& /*req*/, auto& res) {
        auto cfg = ReadAutoTrainConfig();

        Json::Value data;
        data["train_data_exists"] = std::ifstream(cfg.train_data_output).good();
        data["model_exists"]      = std::ifstream(cfg.model_output).good();
        data["train_data_path"]   = cfg.train_data_output;
        data["model_path"]        = cfg.model_output;
        data["dump_tool"]         = cfg.dump_tool;
        data["train_script"]      = cfg.train_script;

        // 事件数量
        int event_count = 0;
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(cfg.events_db.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM search_events", -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) event_count = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }
        data["event_count"] = event_count;

        Json::Value root;
        root["ret"] = 0; root["err_msg"] = ""; root["data"] = std::move(data);
        Json::StreamWriterBuilder w; w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = 200;
    });

    // 触发训练数据 Dump
    server_->Post("/api/v1/admin/model/dump", [](const auto& /*req*/, auto& res) {
        auto cfg = ReadAutoTrainConfig();
        scheduler::AutoTrainTask task(cfg);
        bool ok = task.DumpTrainData();

        Json::Value root;
        root["ret"] = ok ? 0 : 500;
        root["err_msg"] = ok ? "" : "Dump failed";
        root["output"] = cfg.train_data_output;
        Json::StreamWriterBuilder w; w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = ok ? 200 : 500;
    });

    // 触发模型训练
    server_->Post("/api/v1/admin/model/train", [](const auto& /*req*/, auto& res) {
        auto cfg = ReadAutoTrainConfig();
        scheduler::AutoTrainTask task(cfg);
        bool ok = task.RunTrainScript();

        Json::Value root;
        root["ret"] = ok ? 0 : 500;
        root["err_msg"] = ok ? "" : "Training failed";
        root["output"] = cfg.model_output;
        Json::StreamWriterBuilder w; w["indentation"] = "";
        res.set_content(Json::writeString(w, root), "application/json");
        res.status = ok ? 200 : 500;
    });

    // ── Admin 接口（模型热更新，走框架统一入口）──
    server_->Post("/api/v1/admin/reload_model", [](const auto& req, auto& res) {
        if (req.body.empty()) {
            res.set_content(R"({"ret":400,"err_msg":"empty body"})", "application/json");
            res.status = 400;
            return;
        }

        // 解析 model_path
        Json::Value body;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream iss(req.body);
        if (!Json::parseFromStream(builder, iss, &body, &errors) ||
            !body.isMember("model_path") || body["model_path"].asString().empty()) {
            res.set_content(R"({"ret":400,"err_msg":"missing model_path"})", "application/json");
            res.status = 400;
            return;
        }

        std::string model_path = body["model_path"].asString();
        int n = ReloadRankModel(model_path);
        Json::Value resp;
        resp["ret"] = (n > 0) ? 0 : 500;
        resp["err_msg"] = (n > 0) ? "" : "HotReload failed";
        resp["scorers_updated"] = n;
        resp["model_path"] = model_path;

        Json::StreamWriterBuilder w;
        w["indentation"] = "  ";
        res.set_content(Json::writeString(w, resp), "application/json");
        res.status = (n > 0) ? 200 : 500;
    });
}

void HttpServer::Run() {
    std::cout << "[HttpServer] Listening on " << host_ << ":" << port_ << "\n";
    server_->listen(host_.c_str(), port_);
}

void HttpServer::Stop() {
    if (server_) {
        server_->stop();
    }
}

void HttpServer::HandleHealthCheck(const httplib::Request& /*req*/,
                                     httplib::Response& res) {
    auto inv = AppContext::Instance().GetInvertedIndex();
    size_t doc_count  = inv ? inv->GetDocCount()  : 0;
    size_t term_count = inv ? inv->GetTermCount() : 0;

    std::string body = "{\"status\":\"ok\",\"doc_count\":"
                     + std::to_string(doc_count) + ",\"term_count\":"
                     + std::to_string(term_count) + "}";
    res.set_content(body, "application/json");
}

void HttpServer::HandleSearch(const httplib::Request& req,
                                httplib::Response& res) {
    framework::Server::Instance().HandleRequest("search", req, res);
}

void HttpServer::HandleAddDoc(const httplib::Request& req,
                                 httplib::Response& res) {
    DocHandler handler;
    handler.HandleAdd(req, res);
}

void HttpServer::HandleUpdateDoc(const httplib::Request& req,
                                   httplib::Response& res) {
    DocHandler handler;
    handler.HandleUpdate(req, res);
}

void HttpServer::HandleDeleteDoc(const httplib::Request& req,
                                   httplib::Response& res) {
    DocHandler handler;
    handler.HandleDelete(req, res);
}

void HttpServer::HandleGetDoc(const httplib::Request& req,
                                httplib::Response& res) {
    DocHandler handler;
    handler.HandleGet(req, res);
}

void HttpServer::HandleListDocs(const httplib::Request& req,
                                  httplib::Response& res) {
    DocHandler handler;
    handler.HandleList(req, res);
}

void HttpServer::HandleReportEvent(const httplib::Request& req,
                                     httplib::Response& res) {
    EventHandler handler;
    handler.Handle(req, res);
}

void HttpServer::HandleSug(const httplib::Request& req,
                             httplib::Response& res) {
    framework::Server::Instance().HandleRequest("sug", req, res);
}

void HttpServer::HandleHint(const httplib::Request& req,
                              httplib::Response& res) {
    framework::Server::Instance().HandleRequest("hint", req, res);
}

void HttpServer::HandleNav(const httplib::Request& req,
                             httplib::Response& res) {
    framework::Server::Instance().HandleRequest("nav", req, res);
}

} // namespace minisearchrec
