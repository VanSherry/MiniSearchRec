// ============================================================
// MiniSearchRec - Sug 后排序 Processor
// 对标：PostModelRankProcessor + DynamicDeduplicationProcessor
// 功能：最终分数融合 → 排序 → 编辑距离去重 → 截断
// ============================================================

#ifndef MINISEARCHREC_SUG_POST_RANK_PROCESSOR_H
#define MINISEARCHREC_SUG_POST_RANK_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

class SugPostRankProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "SugPostRankProcessor"; }

private:
    // 编辑距离
    static int EditDistance(const std::string& a, const std::string& b);
};

} // namespace minisearchrec

#endif
