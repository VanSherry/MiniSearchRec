#ifndef MINISEARCHREC_HINT_FACTORY_H
#define MINISEARCHREC_HINT_FACTORY_H

#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_factory.h"

namespace minisearchrec {

// ============================================================
// HintRank：相关搜索排序 Pipeline
// ============================================================
class HintRank : public rank::Rank {
public:
    std::string RankName() const override { return "HintRank"; }

protected:
    int PrepareInput() override;
    int GenerateRankOutput() override;
};

// ============================================================
// HintFactory：相关搜索排序工厂
// ============================================================
class HintFactory : public rank::RankFactory {
public:
    rank::BaseRankItem* CreateItem() const override { return new rank::BaseRankItem(); }
    rank::RankContext* CreateContext() const override { return new rank::RankContext(); }
    rank::Rank* CreateRank() const override { return new HintRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HINT_FACTORY_H
