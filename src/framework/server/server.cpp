// ============================================================
// MiniSearchRec - Server 实现
// 对标：通用搜索框架 Server
// ============================================================

#include "framework/server/server.h"
#include "framework/handler/handler_manager.h"
#include "framework/session/session_factory.h"
#include "utils/logger.h"

#include <chrono>
#include <sstream>

// jsoncpp（仅在 HandleRequest 中用于解析 POST body）
#include <json/json.h>

namespace minisearchrec {
namespace framework {

// ============================================================
// Init
// ============================================================
int32_t Server::Init() {
    if (initialized_) {
        LOG_WARN("Server::Init: already initialized");
        return 0;
    }
    LOG_INFO("Server::Init: framework initialized");
    initialized_ = true;
    return 0;
}

// ============================================================
// Search：统一搜索入口
// 对标：通用搜索框架 Server::Search(head_uin, req, resp)
// ============================================================
int32_t Server::Search(const std::string& business_type,
                       const CommonRequest& request,
                       CommonResponse& response) {
    auto time_start = std::chrono::steady_clock::now();

    // 1. 创建 Session
    auto session = SessionFactory::Instance().CreateSession(business_type);
    if (!session) {
        LOG_ERROR("Server::Search: create session failed for '{}'", business_type);
        response.ret = -10001;
        response.err_msg = "session create failed";
        return -1;
    }

    // 2. 初始化 Session
    int32_t ret = session->Init(request);
    if (ret != 0) {
        LOG_ERROR("Server::Search: session init failed, '{}', ret={}", business_type, ret);
        response.ret = -10002;
        response.err_msg = "session init failed";
        return -2;
    }

    // 3. 路由到 Handler
    const BaseHandler* handler = HandlerManager::Instance().GetHandler(business_type);
    if (!handler) {
        LOG_ERROR("Server::Search: handler not found for '{}'", business_type);
        response.ret = -10004;
        response.err_msg = "handler not found";
        return -3;
    }

    // 4. 执行主流程
    ret = handler->Search(session.get());
    if (ret != 0) {
        LOG_ERROR("Server::Search: search failed, '{}', ret={}", business_type, ret);
        response.ret = ret;
        response.err_msg = "search failed";
        return -4;
    }

    // 5. 从 Session 复制响应
    response = session->response;
    return 0;
}

// ============================================================
// HandleRequest：HTTP 层便捷入口
// 解析 HTTP 参数 → CommonRequest → Search → HTTP 响应
// ============================================================
void Server::HandleRequest(const std::string& business_type,
                           const httplib::Request& req,
                           httplib::Response& res) {
    auto start = std::chrono::steady_clock::now();

    // 解析公共请求参数
    CommonRequest request;
    request.business_type = 0;

    // GET 参数
    if (req.has_param("q"))         request.query = req.get_param_value("q");
    if (req.has_param("query"))     request.query = req.get_param_value("query");
    if (req.has_param("uid"))       request.uid = req.get_param_value("uid");
    if (req.has_param("doc_id"))    request.extra["doc_id"] = req.get_param_value("doc_id");
    if (req.has_param("page_size")) {
        try { request.page_size = std::stoi(req.get_param_value("page_size")); } catch (...) {}
    }
    if (req.has_param("page")) {
        try { request.page = std::stoi(req.get_param_value("page")); } catch (...) {}
    }

    // POST body（JSON）
    if (!req.body.empty()) {
        request.raw_body = req.body;
        Json::Value body;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream iss(req.body);
        if (Json::parseFromStream(builder, iss, &body, &errors)) {
            if (body.isMember("query"))     request.query = body["query"].asString();
            if (body.isMember("q"))         request.query = body["q"].asString();
            if (body.isMember("uid"))       request.uid = body["uid"].asString();
            if (body.isMember("doc_id"))    request.extra["doc_id"] = body["doc_id"].asString();
            if (body.isMember("page"))      request.page = body["page"].asInt();
            if (body.isMember("page_size")) request.page_size = body["page_size"].asInt();
            // 将 body 中的所有字段存入 extra
            for (const auto& key : body.getMemberNames()) {
                if (body[key].isString()) {
                    request.extra[key] = body[key].asString();
                } else {
                    Json::StreamWriterBuilder sw;
                    sw["indentation"] = "";
                    request.extra[key] = Json::writeString(sw, body[key]);
                }
            }
        }
    }

    request.req_num = request.page_size;

    // 执行搜索
    CommonResponse response;
    int32_t ret = Search(business_type, request, response);

    auto end_time = std::chrono::steady_clock::now();
    int64_t cost_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start).count();

    // 构造 HTTP 响应
    // 业务的 SetResponse 阶段已经将完整 JSON 写入 items_json
    // 如果业务已经序列化了完整响应（包含 ret/err_msg/results），直接返回
    if (!response.items_json.empty() && response.items_json[0] == '{') {
        // items_json 是完整的 JSON 对象（业务自行序列化）
        // 注入 cost_ms
        // 简单方式：在 JSON 末尾的 } 前插入 cost_ms
        std::string json_str = response.items_json;
        auto last_brace = json_str.rfind('}');
        if (last_brace != std::string::npos) {
            json_str.insert(last_brace,
                ",\n  \"cost_ms\" : " + std::to_string(cost_ms));
        }
        res.set_content(json_str, "application/json");
    } else {
        // 兜底：构造简单 JSON
        Json::Value resp_json;
        resp_json["ret"] = response.ret;
        resp_json["err_msg"] = response.err_msg;
        resp_json["total"] = response.total;
        resp_json["cost_ms"] = static_cast<Json::Int64>(cost_ms);
        resp_json["search_id"] = response.search_id;
        resp_json["results"] = Json::Value(Json::arrayValue);

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "  ";
        res.set_content(Json::writeString(writer, resp_json), "application/json");
    }

    res.status = (response.ret == 0) ? 200 : 500;
}

void Server::Stop() {
    LOG_INFO("Server::Stop: framework shutting down");
}

} // namespace framework
} // namespace minisearchrec
