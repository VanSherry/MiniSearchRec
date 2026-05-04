#include "lib/rank/scorer/quality_rank_processor.h"
#include "utils/logger.h"

namespace minisearchrec {

int QualityRankProcessor::Process() {
    if (!ctx_) return -1;
    float w = config_ ? config_->weight : 1.0f;

    auto vec = ctx_->GetVector();
    int count = 0;
    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = vec->GetItem(i).get();
        if (!item) continue;
        float quality = item->GetFeature("quality");
        float total = item->GetFeature("total_score");
        item->SetFeature("total_score", total + quality * w);
        ++count;
    }

    LOG_DEBUG("QualityRankProcessor: scored {} items (weight={:.2f})", count, w);
    return 0;
}

REGISTER_RANK_PROCESSOR(QualityRankProcessor);
} // namespace minisearchrec
