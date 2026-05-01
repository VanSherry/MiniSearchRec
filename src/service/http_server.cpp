// ============================================================
// MiniSearchRec - HTTP 服务器实现
// 使用 cpp-httplib（header-only）
// ============================================================

#include "service/http_server.h"
#include "service/search_handler.h"
#include "service/doc_handler.h"
#include "service/event_handler.h"
#include "core/app_context.h"
#include <iostream>

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

    // 用户行为上报接口
    server_->Post("/api/v1/event/click", [this](const auto& req, auto& res) {
        HandleReportEvent(req, res);
    });
    server_->Post("/api/v1/event/like", [this](const auto& req, auto& res) {
        HandleReportEvent(req, res);
    });

    // 调试接口：直接查询倒排索引
    server_->Get("/debug/search", [](const auto& req, auto& res) {
        std::string q = req.has_param("q") ? req.get_param_value("q") : "";
        auto inv = AppContext::Instance().GetInvertedIndex();
        if (!inv || q.empty()) {
            res.set_content("{\"error\":\"no index or empty query\"}", "application/json");
            return;
        }
        std::vector<std::string> terms = {q};
        auto ids = inv->Search(terms, 10);
        std::string body = "{\"query\":\"" + q + "\",\"hits\":[";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) body += ",";
            body += "\"" + ids[i] + "\"";
        }
        body += "],\"doc_count\":" + std::to_string(inv->GetDocCount());
        body += ",\"term_count\":" + std::to_string(inv->GetTermCount()) + "}";
        res.set_content(body, "application/json");
    });

    // ── 模型热更新接口 ──
    // POST /api/v1/admin/reload_model
    // Body: {"model_path": "./models/rank_model.txt"}
    // 触发双 Buffer 切换，推理线程不中断
    server_->Post("/api/v1/admin/reload_model", [](const auto& req, auto& res) {
        // 简单 JSON 解析：找 "model_path" 字段
        const std::string& body = req.body;
        std::string model_path;

        auto pos = body.find("\"model_path\"");
        if (pos != std::string::npos) {
            auto q1 = body.find('"', pos + 12);
            if (q1 != std::string::npos) {
                auto q2 = body.find('"', q1 + 1);
                if (q2 != std::string::npos) {
                    model_path = body.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }

        if (model_path.empty()) {
            res.set_content(
                R"({"ret":400,"err_msg":"missing model_path"})",
                "application/json");
            res.status = 400;
            return;
        }

        int n = ReloadRankModel(model_path);
        if (n > 0) {
            res.set_content(
                "{\"ret\":0,\"err_msg\":\"\",\"scorers_updated\":" +
                std::to_string(n) + ",\"model_path\":\"" + model_path + "\"}",
                "application/json");
        } else {
            res.set_content(
                R"({"ret":500,"err_msg":"HotReload failed, check server log"})",
                "application/json");
            res.status = 500;
        }
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
    SearchHandler handler;
    handler.Handle(req, res);
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

void HttpServer::HandleReportEvent(const httplib::Request& req,
                                     httplib::Response& res) {
    EventHandler handler;
    handler.Handle(req, res);
}

} // namespace minisearchrec
