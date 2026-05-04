#include "lib/recall/nav_preset_recall.h"
#include "utils/logger.h"

namespace minisearchrec {

int NavPresetRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    static const char* preset_words[] = {
        "深度学习", "算法面试", "系统设计", "Redis", "分布式系统", "搜索推荐"
    };

    std::vector<DocCandidate> docs;
    for (const auto* pw : preset_words) {
        DocCandidate cand;
        cand.doc_id = pw;
        cand.recall_source = "preset";
        cand.recall_score = 0.0f;
        cand.debug_scores["heat"] = 0.5f;  // 中性默认热度
        ctx->output->doc_scores.emplace_back(pw, 0.0f);
        docs.push_back(std::move(cand));
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("NavPresetRecall: results={}", ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(NavPresetRecall);
