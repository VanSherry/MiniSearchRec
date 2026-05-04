#ifndef MINISEARCHREC_QUALITY_RANK_PROCESSOR_H
#define MINISEARCHREC_QUALITY_RANK_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

// 质量分粗排 Processor
// 读取 "quality" 特征，加权累加到 "total_score"
class QualityRankProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "QualityRankProcessor"; }
};

} // namespace minisearchrec

#endif
