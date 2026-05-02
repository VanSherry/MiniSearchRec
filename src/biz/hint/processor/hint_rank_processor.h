// ============================================================
// MiniSearchRec - Hint 排序 Processor
// 对标：ClickHintBaseDeepFMRankProcessor + PostRankProcessor
// 功能：规则打分 → 排序 → 去重 → 截断
// ============================================================

#ifndef MINISEARCHREC_HINT_RANK_PROCESSOR_H
#define MINISEARCHREC_HINT_RANK_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

// ── HintScoreProcessor：规则打分（模拟 DeepFM）──
class HintScoreProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "HintScoreProcessor"; }
};

// ── HintPostRankProcessor：去重 + MMR + 截断 ──
class HintPostRankProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "HintPostRankProcessor"; }

private:
    static int EditDistance(const std::string& a, const std::string& b);
};

} // namespace minisearchrec

#endif
