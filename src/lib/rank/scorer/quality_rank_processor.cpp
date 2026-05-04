#include "lib/rank/scorer/quality_rank_processor.h"
#include "utils/logger.h"

namespace minisearchrec {

int QualityRankProcessor::Init(const rank::ProcessorConfig* config) {
    config_ = config;
    weight_ = config ? config->weight : 1.0f;
    return 0;
}

int QualityRankProcessor::Process(std::vector<rank::RankItem>& items) {
    for (auto& item : items) {
        float qual   = item.GetFeature("quality");
        float total  = item.GetFeature("total_score");
        item.SetFeature("total_score", total + qual * weight_);
    }
    return 0;
}

REGISTER_RANK_PROCESSOR(QualityRankProcessor);
} // namespace minisearchrec
