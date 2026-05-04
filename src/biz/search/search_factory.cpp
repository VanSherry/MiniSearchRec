#include "biz/search/search_factory.h"
#include "framework/app_context.h"
#include "lib/index/inverted_index.h"
#include "lib/rank/base/rank_manager.h"
#include "utils/logger.h"
#include <cmath>

namespace minisearchrec {

// ============================================================
// PrepareInput：DocCandidate → SearchRankItem（全量特征 KV 化）
// ============================================================
int SearchRank::PrepareInput() {
    auto* ctx = static_cast<SearchContext*>(ctx_.get());
    auto* ss = ctx->Session();
    if (!ss) return -1;

    ctx->stage = GetStage();

    auto& source = (GetStage() == "fine")
        ? ss->coarse_rank_results
        : ss->recall_results;

    if (source.empty()) {
        LOG_INFO("SearchRank::PrepareInput: empty source for stage={}", GetStage());
        return 0;
    }

    auto vec = ctx_->GetVector();
    vec->Clear();

    auto* inverted = AppContext::Instance().GetInvertedIndex().get();
    float avg_doc_len = inverted ? inverted->GetAvgDocLen() : 500.0f;
    const auto& terms = ss->qp_info.terms;
    int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (int i = 0; i < (int)source.size(); ++i) {
        auto& cand = source[i];
        auto item = std::make_shared<SearchRankItem>();
        item->SetWord(cand.doc_id);
        item->cand_index = i;

        // ── 特征 1: BM25 ──
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
        item->SetFeature("bm25", bm25);

        // ── 特征 2: quality ──
        float click_s = std::tanh(std::log1pf((float)cand.click_count) / 5.0f);
        float like_s  = std::tanh(std::log1pf((float)cand.like_count) / 5.0f);
        float qual_s  = std::min(1.0f, std::max(0.0f, cand.quality_score));
        float quality = click_s * 0.3f + like_s * 0.4f + qual_s * 0.3f;
        item->SetFeature("quality", quality);

        // ── 特征 3: freshness ──
        float age_days = cand.publish_time > 0
            ? (float)(now_sec - cand.publish_time) / 86400.0f : 9999.0f;
        float freshness = (age_days >= 0 && age_days <= 365.0f)
            ? std::exp(-0.01f * age_days) : 0.0f;
        item->SetFeature("freshness", freshness);

        // ── 特征 4-10: LGBM 精排用 ──
        item->SetFeature("query_len", std::tanh((float)terms.size() / 5.0f));
        item->SetFeature("log_click", std::tanh(std::log1pf((float)cand.click_count) / 5.0f));
        item->SetFeature("log_like",  std::tanh(std::log1pf((float)cand.like_count) / 5.0f));
        item->SetFeature("title_len", std::tanh((float)cand.title.size() / 30.0f));

        int tag_match = 0;
        for (const auto& term : terms) {
            if (!term.empty() && (cand.title.find(term) != std::string::npos ||
                                  cand.category.find(term) != std::string::npos))
                ++tag_match;
        }
        item->SetFeature("tag_match", std::tanh((float)tag_match / 3.0f));

        float cat_match = 0.0f;
        if (ss->user_profile && !cand.category.empty()) {
            auto it = ss->user_profile->category_weights().find(cand.category);
            if (it != ss->user_profile->category_weights().end())
                cat_match = std::min(1.0f, it->second * 2.0f);
        }
        item->SetFeature("cat_match", cat_match);

        float src_id = 0.0f;
        if (cand.recall_source == "vector")         src_id = 1.0f / 3.0f;
        else if (cand.recall_source == "hot_content") src_id = 2.0f / 3.0f;
        else if (cand.recall_source == "user_history") src_id = 1.0f;
        item->SetFeature("recall_source_id", src_id);

        vec->PushBack(item);
    }

    LOG_INFO("SearchRank::PrepareInput: stage={}, items={}", GetStage(), vec->Size());
    return 0;
}

// ============================================================
// GenerateRankOutput：SearchRankItem KV → DocCandidate
// ============================================================
int SearchRank::GenerateRankOutput() {
    auto* ctx = static_cast<SearchContext*>(ctx_.get());
    auto* ss = ctx->Session();
    if (!ss) return -1;
    auto& target = (GetStage() == "fine")
        ? ss->coarse_rank_results
        : ss->recall_results;

    auto vec = ctx_->GetVector();
    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = static_cast<SearchRankItem*>(vec->GetItem(i).get());
        if (!item || item->cand_index < 0 || item->cand_index >= (int)target.size()) continue;

        auto& cand = target[item->cand_index];
        float coarse = item->GetFeature("total_score");
        float fine   = item->GetFeature("lgbm_score");

        cand.coarse_score = coarse;
        cand.fine_score   = fine;
        cand.final_score  = fine > 0 ? fine : coarse;

        cand.debug_scores["bm25"]      = item->GetFeature("bm25");
        cand.debug_scores["quality"]   = item->GetFeature("quality");
        cand.debug_scores["freshness"] = item->GetFeature("freshness");
        if (fine > 0) cand.debug_scores["lgbm"] = fine;
    }

    LOG_INFO("SearchRank::GenerateRankOutput: stage={}, done", GetStage());
    return 0;
}

// 注册 SearchFactory
REGISTER_RANK_FACTORY(SearchFactory);

} // namespace minisearchrec
