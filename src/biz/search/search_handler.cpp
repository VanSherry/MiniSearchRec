#include "framework/class_register.h"
#include "biz/search/search_handler.h"
#include "lib/rank/scorer/lgbm_rank_processor.h"
#include "framework/processor/dag_pipeline.h"
#include "framework/processor/processor_pipeline.h"
#include "framework/config/config_manager.h"
#include "framework/app_context.h"
#include "lib/query/query_parser.h"
#include "lib/recall/recall_fusion.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"

#include <json/json.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace minisearchrec {

int32_t SearchBizHandler::ExtraInit() {
    LOG_INFO("SearchBizHandler::ExtraInit: ready");
    return 0;
}

SearchSession* SearchBizHandler::GetSearchSession(framework::Session* session) const {
    return dynamic_cast<SearchSession*>(session);
}

// ============================================================
// BuildRankInput：从 SearchSession 构建 RankItem（全量特征预计算）
// rank 阶段从 recall_results 读取，rerank 阶段从 coarse_rank_results 读取
// ============================================================
std::vector<rank::RankItem> SearchBizHandler::BuildRankInput(
    framework::Session* session, const std::string& stage) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return {};

    auto& source = (stage == "rerank") ? ss->coarse_rank_results : ss->recall_results;
    if (source.empty()) return {};

    auto* inverted = AppContext::Instance().GetInvertedIndex().get();
    float avg_doc_len = inverted ? inverted->GetAvgDocLen() : 500.0f;
    const auto& terms = ss->qp_info.terms;
    int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<rank::RankItem> result;
    result.reserve(source.size());

    for (int i = 0; i < (int)source.size(); ++i) {
        auto& cand = source[i];
        rank::RankItem item;
        item.id = cand.doc_id;
        item.source = cand.recall_source;

        // 存映射索引（ApplyRankOutput 写回用）
        item.SetFeature("_cand_idx", (float)i);

        // 特征 1: BM25
        float bm25 = 0.0f;
        if (inverted && !terms.empty()) {
            auto postings = inverted->GetDocPostings(cand.doc_id);
            for (const auto& term : terms) {
                auto pit = postings.find(term);
                if (pit == postings.end()) continue;
                float tf = (float)pit->second.term_freq;
                float field_weight = pit->second.field_weight;
                float idf = ss->qp_info.term_idf.count(term)
                    ? ss->qp_info.term_idf.at(term)
                    : (inverted ? inverted->CalculateIDF(term) : 1.0f);
                float norm_len = avg_doc_len > 1e-6f
                    ? (float)pit->second.doc_len / avg_doc_len : 1.0f;
                bm25 += idf * (tf * 2.5f) / (tf + 1.5f * (1.0f - 0.75f + 0.75f * norm_len)) * field_weight;
            }
            bm25 = std::tanh(bm25 / 10.0f);
        }
        item.SetFeature("bm25", bm25);

        // 特征 2: quality
        float click_s = std::tanh(std::log1pf((float)cand.click_count) / 5.0f);
        float like_s  = std::tanh(std::log1pf((float)cand.like_count) / 5.0f);
        float qual_s  = std::min(1.0f, std::max(0.0f, cand.quality_score));
        item.SetFeature("quality", click_s * 0.3f + like_s * 0.4f + qual_s * 0.3f);

        // 特征 3: freshness
        float age_days = cand.publish_time > 0
            ? (float)(now_sec - cand.publish_time) / 86400.0f : 9999.0f;
        item.SetFeature("freshness", (age_days >= 0 && age_days <= 365.0f)
            ? std::exp(-0.01f * age_days) : 0.0f);

        // 特征 4-10: LGBM 用
        item.SetFeature("query_len", std::tanh((float)terms.size() / 5.0f));
        item.SetFeature("log_click", std::tanh(std::log1pf((float)cand.click_count) / 5.0f));
        item.SetFeature("log_like",  std::tanh(std::log1pf((float)cand.like_count) / 5.0f));
        item.SetFeature("title_len", std::tanh((float)cand.title.size() / 30.0f));

        int tag_match = 0;
        for (const auto& term : terms) {
            if (!term.empty() && (cand.title.find(term) != std::string::npos ||
                                  cand.category.find(term) != std::string::npos))
                ++tag_match;
        }
        item.SetFeature("tag_match", std::tanh((float)tag_match / 3.0f));

        float cat_match = 0.0f;
        if (ss->user_profile && !cand.category.empty()) {
            auto it = ss->user_profile->category_weights().find(cand.category);
            if (it != ss->user_profile->category_weights().end())
                cat_match = std::min(1.0f, it->second * 2.0f);
        }
        item.SetFeature("cat_match", cat_match);

        float src_id = 0.0f;
        if (cand.recall_source == "vector")         src_id = 1.0f / 3.0f;
        else if (cand.recall_source == "hot_content") src_id = 2.0f / 3.0f;
        else if (cand.recall_source == "user_history") src_id = 1.0f;
        item.SetFeature("recall_source_id", src_id);

        result.push_back(std::move(item));
    }

    LOG_INFO("SearchBizHandler::BuildRankInput: stage={}, items={}", stage, result.size());
    return result;
}

// ============================================================
// ApplyRankOutput：将 RankItem 分数写回 DocCandidate
// ============================================================
void SearchBizHandler::ApplyRankOutput(framework::Session* session,
                                        std::vector<rank::RankItem>& items,
                                        const std::string& stage) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return;

    auto& target = (stage == "rerank") ? ss->coarse_rank_results : ss->recall_results;

    for (auto& item : items) {
        int cand_idx = (int)item.GetFeature("_cand_idx", -1.0f);
        if (cand_idx < 0 || cand_idx >= (int)target.size()) continue;

        auto& cand = target[cand_idx];
        float total = item.GetFeature("total_score");

        if (stage == "rerank") {
            float fine = item.GetFeature("lgbm_score");
            cand.fine_score  = fine;
            cand.final_score = fine > 0 ? fine : total;
            if (fine > 0) cand.debug_scores["lgbm"] = fine;
        } else {
            cand.coarse_score = total;
            cand.final_score  = total;
        }
        cand.debug_scores["bm25"]      = item.GetFeature("bm25");
        cand.debug_scores["quality"]   = item.GetFeature("quality");
        cand.debug_scores["freshness"] = item.GetFeature("freshness");
    }
}

// ============================================================
// AfterRank：粗排后处理
// ============================================================
int32_t SearchBizHandler::AfterRank(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;

    std::sort(ss->recall_results.begin(), ss->recall_results.end(),
        [](const DocCandidate& a, const DocCandidate& b) { return a.coarse_score > b.coarse_score; });

    int top_k = 500;
    if (ss->ab_override.coarse_top_k > 0) top_k = ss->ab_override.coarse_top_k;
    if ((int)ss->recall_results.size() > top_k) ss->recall_results.resize(top_k);

    ss->coarse_rank_results = ss->recall_results;
    ss->search_counts.coarse_count = ss->coarse_rank_results.size();

    LOG_INFO("SearchBizHandler::AfterRank: coarse_count={}", ss->search_counts.coarse_count);
    return 0;
}

// ============================================================
// AfterRerank：精排后处理
// ============================================================
int32_t SearchBizHandler::AfterRerank(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;

    for (auto& c : ss->coarse_rank_results) {
        if (c.final_score == 0.0f) c.final_score = c.fine_score > 0 ? c.fine_score : c.coarse_score;
    }
    std::sort(ss->coarse_rank_results.begin(), ss->coarse_rank_results.end(),
        [](const DocCandidate& a, const DocCandidate& b) { return a.final_score > b.final_score; });

    int top_k = 100;
    if (ss->ab_override.fine_top_k > 0) top_k = ss->ab_override.fine_top_k;
    if ((int)ss->coarse_rank_results.size() > top_k) ss->coarse_rank_results.resize(top_k);

    ss->fine_rank_results = ss->coarse_rank_results;
    ss->search_counts.fine_count = ss->fine_rank_results.size();

    LOG_INFO("SearchBizHandler::AfterRerank: fine_count={}", ss->search_counts.fine_count);
    return 0;
}

// ============================================================
// PreSearch
// ============================================================
int32_t SearchBizHandler::PreSearch(framework::Session* session) const {
    int32_t ret = CommonPreSearch(session);
    if (ret != 0) return ret;
    return ExtraPreSearch(session);
}

int32_t SearchBizHandler::ExtraPreSearch(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;

    auto query_parser = std::make_unique<QueryParser>();
    query_parser->Parse(session->query, ss->qp_info);
    LOG_INFO("SearchBizHandler: parsed query='{}', terms={}", session->query, ss->qp_info.terms.size());
    return 0;
}

// ============================================================
// MergeRecall：合并 DAG 召回结果
// ============================================================
int32_t SearchBizHandler::MergeRecall(
    framework::Session* session,
    const std::vector<framework::RecallOutputPtr>& outputs) const {
    auto* ss = GetSearchSession(session);
    if (!ss) return 0;

    std::vector<std::vector<DocCandidate>> multi_results;
    for (const auto& output : outputs) {
        try {
            auto& cands = std::any_cast<const std::vector<DocCandidate>&>(output->items);
            multi_results.push_back(cands);
        } catch (...) {}
    }

    ss->recall_results = RecallFusion::FuseByRRF(multi_results);
    ss->search_counts.recall_count = static_cast<int>(ss->recall_results.size());

    auto doc_store = AppContext::Instance().GetDocStore();
    if (doc_store) {
        for (auto& cand : ss->recall_results) {
            if (!cand.title.empty()) continue;
            Document doc;
            if (doc_store->GetDoc(cand.doc_id, doc)) {
                cand.title           = doc.title();
                cand.content_snippet = doc.content().substr(0, 200);
                cand.author          = doc.author();
                cand.publish_time    = doc.publish_time();
                cand.category        = doc.category();
                cand.quality_score   = doc.quality_score();
                cand.click_count     = doc.click_count();
                cand.like_count      = doc.like_count();
            }
        }
    }
    LOG_INFO("SearchBizHandler::MergeRecall: merged {} sources → {} candidates",
             multi_results.size(), ss->recall_results.size());
    return 0;
}

// ============================================================
// CanSearch
// ============================================================
bool SearchBizHandler::CanSearch(framework::Session* session) const {
    if (session->query.empty()) { LOG_DEBUG("SearchBizHandler: empty query"); return false; }
    return InterposeCheckQuery(session);
}

// ============================================================
// DoInterpose
// ============================================================
int32_t SearchBizHandler::DoInterpose(framework::Session* session) const {
    auto* pipeline_cfg = framework::PipelineManager::Instance().GetConfig(config_.business_type);
    if (pipeline_cfg) {
        if (pipeline_cfg->filter_pipeline.Size() > 0) {
            auto ret = pipeline_cfg->filter_pipeline.Execute(session);
            if (ret != 0) LOG_WARN("SearchBizHandler: filter_pipeline ret={}", ret);
        }
        if (pipeline_cfg->postprocess_pipeline.Size() > 0) {
            auto ret = pipeline_cfg->postprocess_pipeline.Execute(session);
            if (ret != 0) LOG_WARN("SearchBizHandler: postprocess_pipeline ret={}", ret);
        }
    }
    // final_results 兜底
    auto* ss = GetSearchSession(session);
    if (ss && ss->final_results.empty() && !ss->fine_rank_results.empty()) {
        ss->final_results = ss->fine_rank_results;
    }
    return 0;
}

// ============================================================
// SetResponse
// ============================================================
int32_t SearchBizHandler::SetResponse(framework::Session* session) const {
    auto* ss = GetSearchSession(session);
    if (!ss) { LOG_ERROR("SearchBizHandler: session cast failed"); return -1; }

    int page = session->request.page;
    int page_size = session->request.page_size;
    int start_idx = (page - 1) * page_size;
    int end_idx = std::min(start_idx + page_size, (int)ss->final_results.size());

    Json::Value root;
    root["ret"] = 0; root["err_msg"] = ""; root["total"] = (int)ss->final_results.size();
    root["trace_id"] = session->trace_id; root["page"] = page; root["page_size"] = page_size;
    root["search_id"] = session->search_id;

    Json::Value results(Json::arrayValue);
    for (int i = start_idx; i < end_idx; ++i) {
        const auto& cand = ss->final_results[i];
        Json::Value item;
        item["doc_id"] = cand.doc_id; item["title"] = cand.title;
        item["snippet"] = cand.content_snippet; item["score"] = cand.final_score;
        item["recall_source"] = cand.recall_source; item["author"] = cand.author;
        item["publish_time"] = (Json::Int64)cand.publish_time;
        item["category"] = cand.category;
        item["click_count"] = (Json::Int64)cand.click_count;
        item["like_count"] = (Json::Int64)cand.like_count;

        Json::Value debug(Json::objectValue);
        for (const auto& [k, v] : cand.debug_scores) debug[k] = v;
        item["debug_scores"] = debug;
        results.append(item);
    }
    root["results"] = results;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    session->response.ret = 0; session->response.total = (int)ss->final_results.size();
    session->response.items_json = Json::writeString(writer, root);
    session->response.search_id = session->search_id;
    return 0;
}

// ============================================================
// ReloadRankModel
// ============================================================
int ReloadRankModel(const std::string& new_model_path) {
    if (new_model_path.empty()) { LOG_WARN("ReloadRankModel: empty model path"); return 0; }
    if (!LGBMRankProcessor::HotReload(new_model_path)) {
        LOG_ERROR("ReloadRankModel: HotReload failed: {}", new_model_path);
        return -1;
    }
    LOG_INFO("ReloadRankModel: LGBM model reloaded from {}", new_model_path);
    return 1;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_HANDLER(SearchBizHandler);
