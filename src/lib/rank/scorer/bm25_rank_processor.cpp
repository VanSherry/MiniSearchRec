#include "lib/rank/scorer/bm25_rank_processor.h"
#include "utils/logger.h"

namespace minisearchrec {

int BM25RankProcessor::Init(const rank::ProcessorConfig* config) {
    config_ = config;
    weight_ = config ? config->weight : 1.0f;
    return 0;
}

int BM25RankProcessor::Process(std::vector<rank::RankItem>& items) {
    for (auto& item : items) {
        float bm25   = item.GetFeature("bm25");
        float total  = item.GetFeature("total_score");
        item.SetFeature("total_score", total + bm25 * weight_);
    }
    return 0;
}

REGISTER_RANK_PROCESSOR(BM25RankProcessor);
} // namespace minisearchrec
