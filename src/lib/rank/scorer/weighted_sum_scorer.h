#ifndef MINISEARCHREC_WEIGHTED_SUM_SCORER_H
#define MINISEARCHREC_WEIGHTED_SUM_SCORER_H

#include "lib/rank/engine/rank_engine.h"
#include <vector>

namespace minisearchrec {

class WeightedSumScorer : public rank::ProcessorInterface {
public:
    int Init(const rank::ProcessorConfig* config) override;
    int Process(std::vector<rank::RankItem>& items) override;
    std::string Name() const override { return "WeightedSumScorer"; }

private:
    struct WeightedFeature {
        std::string name;
        float weight = 1.0f;
    };
    std::vector<WeightedFeature> features_;
};

} // namespace minisearchrec

#endif
