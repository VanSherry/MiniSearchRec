// ============================================================
// MiniSearchRec - Hint 排序工厂
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_HINT_FACTORY_H
#define MINISEARCHREC_HINT_FACTORY_H

#include "lib/rank/base/rank_factory.h"
#include "lib/rank/base/rank.h"
#include "biz/hint/hint_rank_item.h"
#include "biz/hint/hint_context.h"

namespace minisearchrec {

class HintRank : public rank::Rank {
public:
    ~HintRank() override = default;
    std::string RankName() const override { return "HintRank"; }

    // Hint 的 PrepareInput：多路召回（标签匹配/分类热门/行为共现/Query扩展）
    int PrepareInput() override;
};

class HintFactory : public rank::RankFactory {
public:
    ~HintFactory() override = default;

    rank::BaseRankItem* CreateItem() const override { return new HintRankItem(); }
    rank::RankVector* CreateVector() const override { return new rank::RankVector(); }
    rank::RankContext* CreateContext() const override { return new HintContext(); }
    rank::Rank* CreateRank() const override { return new HintRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HINT_FACTORY_H
