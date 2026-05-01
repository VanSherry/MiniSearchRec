// ============================================================
// MiniSearchRec - 搜索接口处理器实现
// 对应 API：POST /api/v1/search
// ============================================================

#include "service/search_handler.h"
#include "core/session.h"
#include "core/pipeline.h"
#include "core/config_manager.h"
#include "core/app_context.h"
#include "cache/cache_manager.h"
#include "query/query_parser.h"
#include "utils/logger.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <json/json.h>

namespace minisearchrec {

// ============================================================
// Pipeline 单例：只加载一次配置，多次复用
// ============================================================
static Pipeline& GetSearchPipeline() {
    static Pipeline pipeline;
    static bool initialized = false;
    if (!initialized) {
        // 从 ConfigManager 合并 recall + rank + filter 配置
        const auto& recall_cfg  = ConfigManager::Instance().GetRecallConfig();
        const auto& rank_cfg    = ConfigManager::Instance().GetRankConfig();
        const auto& filter_cfg  = ConfigManager::Instance().GetFilterConfig();
        pipeline.LoadFromNodes(recall_cfg, rank_cfg, filter_cfg);
        initialized = true;
        LOG_INFO("SearchPipeline initialized (singleton)");
    }
    return pipeline;
}

// ============================================================
// CacheManager 单例
// ============================================================
static CacheManager& GetCacheManager() {
    static CacheManager cache(
        ConfigManager::Instance().GetGlobalConfig().cache.local_capacity
    );
    return cache;
}

void SearchHandler::Handle(const httplib::Request& req,
                            httplib::Response& res) {
    SearchRequest request;
    if (!ParseRequest(req.body, request)) {
        res.set_content(R"({"ret":400,"err_msg":"Invalid request body","total":0,"cost_ms":0,"trace_id":"","page":1,"page_size":20,"results":[]})",
                        "application/json");
        res.status = 400;
        return;
    }

    SearchResponse response;
    int ret = DoSearch(request, response);
    std::string json = SerializeResponse(response);
    res.set_content(json, "application/json");
    res.status = (ret == 0) ? 200 : 500;
}

bool SearchHandler::ParseRequest(const std::string& body,
                                   SearchRequest& request) {
    if (body.empty()) return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(body);
    if (!Json::parseFromStream(builder, iss, &root, &errors)) {
        LOG_WARN("SearchHandler::ParseRequest JSON parse failed: {}", errors);
        return false;
    }

    if (root.isMember("query") && root["query"].isString()) {
        request.set_query(root["query"].asString());
    }
    if (root.isMember("uid") && root["uid"].isString()) {
        request.set_uid(root["uid"].asString());
    }
    if (root.isMember("page") && root["page"].isInt()) {
        request.set_page(root["page"].asInt());
    }
    if (root.isMember("page_size") && root["page_size"].isInt()) {
        request.set_page_size(root["page_size"].asInt());
    }
    if (root.isMember("business_type") && root["business_type"].isString()) {
        request.set_business_type(root["business_type"].asString());
    }

    if (request.query().empty()) {
        LOG_WARN("SearchHandler::ParseRequest: empty query");
        return false;
    }
    // page 和 page_size 合法性校验
    if (request.page() < 0) request.set_page(1);
    if (request.page_size() <= 0 || request.page_size() > 100) {
        request.set_page_size(20);
    }
    return true;
}

int SearchHandler::DoSearch(const SearchRequest& request,
                              SearchResponse& response) {
    // 1. 检查缓存
    auto& cache = GetCacheManager();
    std::string cache_key = cache.MakeCacheKey(request);
    auto cached = cache.GetSearchCache(cache_key);
    if (cached) {
        response = std::move(cached.value());
        LOG_INFO("SearchHandler: cache hit for query={}", request.query());
        return 0;
    }

    // 2. 创建 Session
    Session session;
    session.request    = request;
    session.trace_id   = Session::GenerateTraceId();
    session.business_type = request.business_type().empty()
                            ? "default" : request.business_type();

    // 设置请求级 deadline（默认 200ms）
    session.timeout_ms = 200;
    {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        session.deadline_ms = now + session.timeout_ms;
    }

    // 3. Query 理解：使用 QueryParser 分词 + 归一化
    QueryParser qp;
    std::string qp_error;
    if (!qp.ValidateQuery(request.query(), qp_error)) {
        response.set_ret(400);
        response.set_err_msg(qp_error);
        return 400;
    }
    qp.Parse(request.query(), session.qp_info);

    // 4. A/B 实验：根据 uid 分配实验组，通过 GetParam() 覆盖 Pipeline 参数
    auto ab_mgr = AppContext::Instance().GetABTestManager();
    if (ab_mgr && !request.uid().empty()) {
        const auto* exp = ab_mgr->AssignExperiment(request.uid());
        if (exp) {
            session.business_type = exp->name;
            LOG_INFO("SearchHandler: uid={} assigned to experiment={}",
                     request.uid(), exp->name);
        }
        // 读取实验组参数覆盖（对照组走默认值 -1，不覆盖）
        const std::string uid = request.uid();
        std::string v;
        v = ab_mgr->GetParam(uid, "mmr_lambda", "");
        if (!v.empty()) {
            try { session.ab_override.mmr_lambda = std::stof(v); } catch (...) {}
        }
        v = ab_mgr->GetParam(uid, "coarse_top_k", "");
        if (!v.empty()) {
            try { session.ab_override.coarse_top_k = std::stoi(v); } catch (...) {}
        }
        v = ab_mgr->GetParam(uid, "fine_top_k", "");
        if (!v.empty()) {
            try { session.ab_override.fine_top_k = std::stoi(v); } catch (...) {}
        }
    }

    // 4. 预计算 term IDF（需要索引已就绪）
    auto inv_idx = AppContext::Instance().GetInvertedIndex();
    if (inv_idx) {
        for (const auto& term : session.qp_info.terms) {
            if (session.qp_info.term_idf.find(term) == session.qp_info.term_idf.end()) {
                session.qp_info.term_idf[term] = inv_idx->CalculateIDF(term);
            }
        }
    }

    LOG_INFO("SearchHandler: trace_id={}, query={}, terms={}",
             session.trace_id, request.query(), session.qp_info.terms.size());

    // 5. 执行 Pipeline（单例，已加载配置）
    Pipeline& pipeline = GetSearchPipeline();
    int ret = pipeline.Execute(session);
    if (ret != 0) {
        response.set_ret(ret);
        response.set_err_msg("Pipeline execution failed");
        LOG_ERROR("SearchHandler: pipeline failed, trace_id={}, ret={}",
                  session.trace_id, ret);
        return ret;
    }

    response = session.response;

    // 6. 写入缓存（只缓存有结果的请求）
    if (response.results_size() > 0) {
        cache.SetSearchCache(cache_key, response);
    }

    return 0;
}

std::string SearchHandler::SerializeResponse(const SearchResponse& response) {
    Json::Value root;
    root["ret"]       = response.ret();
    root["err_msg"]   = response.err_msg();
    root["total"]     = response.total();
    root["cost_ms"]   = static_cast<Json::Int64>(response.cost_ms());
    root["trace_id"]  = response.trace_id();
    root["page"]      = response.page();
    root["page_size"] = response.page_size();

    Json::Value results(Json::arrayValue);
    for (int i = 0; i < response.results_size(); ++i) {
        const auto& r = response.results(i);
        Json::Value item;
        item["doc_id"]        = r.doc_id();
        item["title"]         = r.title();
        item["snippet"]       = r.snippet();
        item["score"]         = r.score();
        item["doc_url"]       = r.doc_url();
        item["recall_source"] = r.recall_source();
        item["author"]        = r.author();
        item["publish_time"]  = static_cast<Json::Int64>(r.publish_time());
        item["click_count"]   = static_cast<Json::Int64>(r.click_count());
        item["like_count"]    = static_cast<Json::Int64>(r.like_count());

        Json::Value debug(Json::objectValue);
        for (const auto& [key, val] : r.debug_scores()) {
            debug[key] = val;
        }
        item["debug_scores"] = debug;
        results.append(item);
    }
    root["results"] = results;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    return Json::writeString(writer, root);
}

} // namespace minisearchrec
