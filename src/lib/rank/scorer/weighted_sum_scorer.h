#ifndef MINISEARCHREC_WEIGHTED_SUM_SCORER_H
#define MINISEARCHREC_WEIGHTED_SUM_SCORER_H

#include "lib/rank/base/processor_interface.h"
#include <vector>

namespace minisearchrec {

// 通用加权求和打分器
// 从 KV 特征中按配置读取特征名+权重，计算加权总分
class WeightedSumScorer : public rank::ProcessorInterface {
public:
    int Init(rank::RankContextPtr ctx, const rank::ProcessorConfig* config) override;
    int Process() override;
    std::string Name() const override { return "WeightedSumScorer"; }

private:
    struct WeightedFeature {
        std::string name;
        float weight = 1.0f;
    };
    std::vector<WeightedFeature> features_;
    float weight_ = 1.0f;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_WEIGHTED_SUM_SCORER_H
