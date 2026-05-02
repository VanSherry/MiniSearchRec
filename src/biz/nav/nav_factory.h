// ============================================================
// MiniSearchRec - Nav 排序工厂
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_NAV_FACTORY_H
#define MINISEARCHREC_NAV_FACTORY_H

#include "lib/rank/base/rank_factory.h"
#include "lib/rank/base/rank.h"
#include "biz/nav/nav_rank_item.h"
#include "biz/nav/nav_context.h"

namespace minisearchrec {

class NavRank : public rank::Rank {
public:
    ~NavRank() override = default;
    std::string RankName() const override { return "NavRank"; }

    // Nav 的 PrepareInput：全局热词 + 预置词兜底
    int PrepareInput() override;
};

class NavFactory : public rank::RankFactory {
public:
    ~NavFactory() override = default;

    rank::BaseRankItem* CreateItem() const override { return new NavRankItem(); }
    rank::RankVector* CreateVector() const override { return new rank::RankVector(); }
    rank::RankContext* CreateContext() const override { return new NavContext(); }
    rank::Rank* CreateRank() const override { return new NavRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_NAV_FACTORY_H
