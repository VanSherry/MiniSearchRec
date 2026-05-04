#include "lib/rank/scorer/freshness_rank_processor.h"
#include "utils/logger.h"

namespace minisearchrec {

int FreshnessRankProcessor::Init(const rank::ProcessorConfig* config) {
    config_ = config;
    weight_ = config ? config->weight : 1.0f;
    return 0;
}

int FreshnessRankProcessor::Process(std::vector<rank::RankItem>& items) {
    for (auto& item : items) {
        float fresh = item.GetFeature("freshness");
        float total = item.GetFeature("total_score");
        item.SetFeature("total_score", total + fresh * weight_);
    }
    return 0;
}

REGISTER_RANK_PROCESSOR(FreshnessRankProcessor);
} // namespace minisearchrec
