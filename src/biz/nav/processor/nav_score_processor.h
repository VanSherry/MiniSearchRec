// ============================================================
// MiniSearchRec - Nav 评分 + 多样性 Processor
// 对标：QXNavRank 的 Adams 模型简化版 + MMR 多样性
// ============================================================

#ifndef MINISEARCHREC_NAV_SCORE_PROCESSOR_H
#define MINISEARCHREC_NAV_SCORE_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

class NavScoreProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "NavScoreProcessor"; }
};

} // namespace minisearchrec

#endif
