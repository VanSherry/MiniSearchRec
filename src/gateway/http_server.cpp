// ============================================================
// MiniSearchRec - HTTP 服务器实现
// 使用 cpp-httplib（header-only）
// ============================================================

#include "gateway/http_server.h"
#include "framework/server/server.h"
#include "biz/doc/doc_handler.h"
#include "biz/event/event_handler.h"
#include "biz/search/search_handler.h"
#include "framework/app_context.h"
#include <iostream>
#include <sstream>
#include <json/json.h>

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
