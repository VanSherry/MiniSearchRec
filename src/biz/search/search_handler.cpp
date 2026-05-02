// ============================================================
// MiniSearchRec - 垂搜 Handler 实现
//
// 架构：
//   框架 BaseHandler::Search() 主流程模板方法
//     → PreSearch: ExtraPreSearch 做 QP + AB
//     → DoSearch:  CommonDoSearch 调用框架 recall_pipeline
//     → DoRank:    CommonDoRank 调用框架 rank_pipeline + AfterRank 截断
//     → DoRerank:  CommonDoRerank 调用框架 rerank_pipeline + AfterRerank 截断
//     → DoInterpose: 调用框架 filter_pipeline + postprocess_pipeline
//     → SetResponse: 分页 + JSON
//
// 去掉了旧的 SearchRank/SearchContext/SearchFactory/Pipeline 适配层
// ============================================================

#include "biz/search/search_handler.h"
#include "framework/processor/processor_pipeline.h"
#include "framework/config/config_manager.h"
#include "framework/app_context.h"
#include "lib/query/query_parser.h"
#include "utils/logger.h"

#include <json/json.h>
#include <algorithm>
#include <chrono>

namespace minisearchrec {

// ============================================================
// ExtraInit：业务初始化（框架注册 Handler 时自动调用）
// Search 的 Processor Pipeline 由框架从 biz/search.yaml 自动加载，无需手动注册
// ============================================================
int32_t SearchBizHandler::ExtraInit() {
    LOG_INFO("SearchBizHandler::ExtraInit: ready (pipeline config-driven)");
    return 0;
}

// ============================================================
// GetSearchSession：向下转型
// ============================================================
SearchSession* SearchBizHandler::GetSearchSession(framework::Session* s) {
    return dynamic_cast<SearchSession*>(s);
}

// ============================================================
// ExtraPreSearch：QP 理解 + AB 实验
// ============================================================
int32_t SearchBizHandler::ExtraPreSearch(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;  // 非 SearchSession，跳过

    // Query 理解
    QueryParser parser;
    ss->qp_info = QPInfo{};
    parser.Parse(session->query, ss->qp_info);

    // AB 实验（从配置读取覆盖参数）
    auto ab_str = session->Get("ab_coarse_top_k");
    if (!ab_str.empty()) {
        try { ss->ab_override.coarse_top_k = std::stoi(ab_str); } catch (...) {}
    }
    ab_str = session->Get("ab_fine_top_k");
    if (!ab_str.empty()) {
        try { ss->ab_override.fine_top_k = std::stoi(ab_str); } catch (...) {}
    }
    ab_str = session->Get("ab_mmr_lambda");
    if (!ab_str.empty()) {
        try { ss->ab_override.mmr_lambda = std::stof(ab_str); } catch (...) {}
    }

    return 0;
}

// ============================================================
// CommonDoSearch：调用框架 recall_pipeline
// 对标通用搜索框架 DoSearch
// ============================================================
int32_t SearchBizHandler::CommonDoSearch(framework::Session* session) const {
    // 框架层会自动根据 business_type="search" 找到 recall_pipeline 并执行
    auto* pipeline_cfg = framework::PipelineManager::Instance().GetConfig("search");
    if (!pipeline_cfg || pipeline_cfg->recall_pipeline.Size() == 0) {
        LOG_WARN("SearchBizHandler::CommonDoSearch: no recall pipeline configured");
        return 0;
    }
    return pipeline_cfg->recall_pipeline.Execute(session);
}

// ============================================================
// AfterRank：粗排后截断
// ============================================================
int32_t SearchBizHandler::AfterRank(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;

    // 按 coarse_score 排序
    std::sort(ss->recall_results.begin(), ss->recall_results.end(),
        [](const DocCandidate& a, const DocCandidate& b) {
            return a.coarse_score > b.coarse_score;
        });

    // 截断
    int top_k = 500;
    if (ss->ab_override.coarse_top_k > 0) {
        top_k = ss->ab_override.coarse_top_k;
    }
    if ((int)ss->recall_results.size() > top_k) {
        ss->recall_results.resize(top_k);
    }

    // 粗排结果 → coarse_rank_results
    ss->coarse_rank_results = ss->recall_results;
    ss->search_counts.coarse_count = ss->coarse_rank_results.size();

    return 0;
}

// ============================================================
// AfterRerank：精排后截断
// ============================================================
int32_t SearchBizHandler::AfterRerank(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;

    // 按 fine_score 排序
    for (auto& c : ss->coarse_rank_results) {
        if (c.fine_score == 0.0f) c.fine_score = c.coarse_score;
        c.final_score = c.fine_score;
    }
    std::sort(ss->coarse_rank_results.begin(), ss->coarse_rank_results.end(),
        [](const DocCandidate& a, const DocCandidate& b) {
            return a.fine_score > b.fine_score;
        });

    int top_k = 100;
    if (ss->ab_override.fine_top_k > 0) {
        top_k = ss->ab_override.fine_top_k;
    }
    if ((int)ss->coarse_rank_results.size() > top_k) {
        ss->coarse_rank_results.resize(top_k);
    }

    ss->fine_rank_results = ss->coarse_rank_results;
    ss->search_counts.fine_count = ss->fine_rank_results.size();

    return 0;
}

// ============================================================
// DoInterpose：过滤 + 后处理
// ============================================================
int32_t SearchBizHandler::DoInterpose(framework::Session* session) const {
    auto* pipeline_cfg = framework::PipelineManager::Instance().GetConfig("search");
    if (!pipeline_cfg) return 0;

    // 过滤
    if (pipeline_cfg->filter_pipeline.Size() > 0) {
        pipeline_cfg->filter_pipeline.Execute(session);
    }

    // 后处理（MMR 等）
    if (pipeline_cfg->postprocess_pipeline.Size() > 0) {
        pipeline_cfg->postprocess_pipeline.Execute(session);
    }

    // 最终结果
    auto* ss = GetSearchSession(session);
    if (ss) {
        ss->final_results = ss->fine_rank_results;
        ss->search_counts.final_count = ss->final_results.size();
    }

    return 0;
}

// ============================================================
// SetResponse：分页 + JSON 序列化
// ============================================================
int32_t SearchBizHandler::SetResponse(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) {
        session->response.ret = -1;
        session->response.err_msg = "invalid session type";
        return -1;
    }

    int page = session->request.page > 0 ? session->request.page : 1;
    int page_size = session->request.page_size > 0 ? session->request.page_size : 20;
    int start_idx = (page - 1) * page_size;
    int end_idx = std::min(start_idx + page_size, (int)ss->final_results.size());

    Json::Value resp_root;
    resp_root["ret"] = 0;
    resp_root["err_msg"] = "";
    resp_root["total"] = (int)ss->final_results.size();
    resp_root["trace_id"] = session->trace_id;
    resp_root["page"] = page;
    resp_root["page_size"] = page_size;
    resp_root["search_id"] = session->search_id;

    Json::Value results(Json::arrayValue);
    for (int i = start_idx; i < end_idx; ++i) {
        const auto& cand = ss->final_results[i];
        Json::Value item;
        item["doc_id"] = cand.doc_id;
        item["title"] = cand.title;
        item["snippet"] = cand.content_snippet;
        item["score"] = cand.final_score;
        item["recall_source"] = cand.recall_source;
        item["author"] = cand.author;
        item["publish_time"] = static_cast<Json::Int64>(cand.publish_time);
        item["category"] = cand.category;
        item["click_count"] = static_cast<Json::Int64>(cand.click_count);
        item["like_count"] = static_cast<Json::Int64>(cand.like_count);

        Json::Value debug(Json::objectValue);
        for (const auto& [key, val] : cand.debug_scores) {
            debug[key] = val;
        }
        item["debug_scores"] = debug;
        results.append(item);
    }
    resp_root["results"] = results;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    session->response.ret = 0;
    session->response.total = (int)ss->final_results.size();
    session->response.items_json = Json::writeString(writer, resp_root);
    session->response.search_id = session->search_id;

    return 0;
}

// ============================================================
// ReloadRankModel：通过框架 PipelineManager 热更新
// ============================================================
int ReloadRankModel(const std::string& new_model_path) {
    auto* pipeline_cfg = framework::PipelineManager::Instance().GetConfig("search");
    if (!pipeline_cfg) {
        LOG_WARN("ReloadRankModel: no search pipeline config");
        return 0;
    }

    // 在 rerank_pipeline 中找到 LGBMScorerProcessor 并热更新
    int count = 0;
    for (auto& proc : pipeline_cfg->rerank_pipeline.GetProcessors()) {
        if (!proc) continue;
        if (proc->Name() == "LGBMScorerProcessor") {
            // 触发 HotReload（需要 LGBMScorerProcessor 实现 HotReload 方法）
            // TODO: 通过 dynamic_cast 调用特定方法
            LOG_INFO("ReloadRankModel: found LGBMScorerProcessor, reload path={}",
                     new_model_path);
            ++count;
        }
    }
    return count;
}

} // namespace minisearchrec

// 注册到框架
using namespace minisearchrec;
REGISTER_MSR_HANDLER(SearchBizHandler);
