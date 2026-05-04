#ifndef MINISEARCHREC_FRESHNESS_RANK_PROCESSOR_H
#define MINISEARCHREC_FRESHNESS_RANK_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

// 时效性粗排 Processor
// 读取 "freshness" 特征，加权累加到 "total_score"
class FreshnessRankProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "FreshnessRankProcessor"; }
};

} // namespace minisearchrec

#endif
