#ifndef MINISEARCHREC_NAV_FACTORY_H
#define MINISEARCHREC_NAV_FACTORY_H

#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_factory.h"

namespace minisearchrec {

// ============================================================
// NavRank：导航页排序 Pipeline
// ============================================================
class NavRank : public rank::Rank {
public:
    std::string RankName() const override { return "NavRank"; }

protected:
    int PrepareInput() override;
    int GenerateRankOutput() override;
};

// ============================================================
// NavFactory：导航页排序工厂
// ============================================================
class NavFactory : public rank::RankFactory {
public:
    rank::BaseRankItem* CreateItem() const override { return new rank::BaseRankItem(); }
    rank::RankContext* CreateContext() const override { return new rank::RankContext(); }
    rank::Rank* CreateRank() const override { return new NavRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_NAV_FACTORY_H
