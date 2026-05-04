#include "lib/recall/nav_global_hot_recall.h"
#include "lib/storage/query_stats_store.h"
#include "utils/logger.h"

namespace minisearchrec {

int NavGlobalHotRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    std::vector<DocCandidate> docs;
    float max_freq = 1.0f;

    for (const auto& q : QueryStatsStore::Instance().GetTopN(30)) {
        if (q.query.empty()) continue;
        DocCandidate cand;
        cand.doc_id = q.query;
        cand.recall_source = "hot";
        cand.recall_score = 0.0f;
        cand.click_count = q.freq;
        max_freq = std::max(max_freq, (float)q.freq);
        ctx->output->doc_scores.emplace_back(q.query, 0.0f);
        docs.push_back(std::move(cand));
    }

    for (auto& d : docs) {
        d.debug_scores["heat"] = std::log1pf((float)d.click_count) / std::log1pf(max_freq);
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("NavGlobalHotRecall: results={}", ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(NavGlobalHotRecall);
