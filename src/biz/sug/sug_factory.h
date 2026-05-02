// ============================================================
// MiniSearchRec - Sug 排序工厂
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_SUG_FACTORY_H
#define MINISEARCHREC_SUG_FACTORY_H

#include "lib/rank/base/rank_factory.h"
#include "lib/rank/base/rank.h"
#include "biz/sug/sug_rank_item.h"
#include "biz/sug/sug_context.h"

namespace minisearchrec {

class SugRank : public rank::Rank {
public:
    ~SugRank() override = default;
    std::string RankName() const override { return "SugRank"; }

    // Sug 的 PrepareInput：从 Trie 召回候选词并填入 RankVector
    int PrepareInput() override;
};

class SugFactory : public rank::RankFactory {
public:
    ~SugFactory() override = default;

    rank::BaseRankItem* CreateItem() const override { return new SugRankItem(); }
    rank::RankVector* CreateVector() const override { return new rank::RankVector(); }
    rank::RankContext* CreateContext() const override { return new SugContext(); }
    rank::Rank* CreateRank() const override { return new SugRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SUG_FACTORY_H
