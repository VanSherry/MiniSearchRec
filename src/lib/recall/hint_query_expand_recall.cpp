#include "lib/recall/hint_query_expand_recall.h"
#include "lib/storage/query_stats_store.h"
#include "utils/logger.h"

namespace minisearchrec {

int HintQueryExpandRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    if (!ctx->session) return 0;
    if (ctx->session->query.empty()) return 0;

    std::string prefix = ctx->session->query.substr(0, std::min((size_t)6, ctx->session->query.size()));
    float max_freq = 1.0f;

    auto queries = QueryStatsStore::Instance().GetByPrefix(prefix, 15);
    if (queries.empty()) { LOG_DEBUG("HintQueryExpandRecall: no prefix matches for '{}'", prefix); return 0; }

    for (const auto& qi : queries) {
        if (qi.query == ctx->session->query) continue;
        max_freq = std::max(max_freq, (float)qi.freq);
    }

    std::vector<DocCandidate> docs;
    for (const auto& qi : queries) {
        if (qi.query == ctx->session->query) continue;
        DocCandidate cand;
        cand.doc_id = qi.query;
        cand.recall_source = "query_expand";
        cand.recall_score = 0.0f;
        float heat = std::log1pf((float)qi.freq) / std::log1pf(max_freq);
        cand.debug_scores["query_relevance"] = heat;
        ctx->output->doc_scores.emplace_back(qi.query, 0.0f);
        docs.push_back(std::move(cand));
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("HintQueryExpandRecall: prefix='{}', results={}", prefix, ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(HintQueryExpandRecall);
