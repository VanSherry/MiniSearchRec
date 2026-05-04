#ifndef MINISEARCHREC_FRESHNESS_RANK_PROCESSOR_H
#define MINISEARCHREC_FRESHNESS_RANK_PROCESSOR_H

#include "lib/rank/engine/rank_engine.h"

namespace minisearchrec {

class FreshnessRankProcessor : public rank::ProcessorInterface {
public:
    int Init(const rank::ProcessorConfig* config) override;
    int Process(std::vector<rank::RankItem>& items) override;
    std::string Name() const override { return "FreshnessRankProcessor"; }
};

} // namespace minisearchrec

#endif
