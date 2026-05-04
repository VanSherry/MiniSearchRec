#ifndef MINISEARCHREC_SUG_FACTORY_H
#define MINISEARCHREC_SUG_FACTORY_H

#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_factory.h"

namespace minisearchrec {

// ============================================================
// SugRank：搜索建议排序 Pipeline
// PrepareInput: 从 any_store 读取 DocCandidate 转为 BaseRankItem
// GenerateRankOutput: 排序截断，写入 any_store
// ============================================================
class SugRank : public rank::Rank {
public:
    std::string RankName() const override { return "SugRank"; }

protected:
    int PrepareInput() override;
    int GenerateRankOutput() override;
};

// ============================================================
// SugFactory：搜索建议排序工厂
// ============================================================
class SugFactory : public rank::RankFactory {
public:
    rank::BaseRankItem* CreateItem() const override { return new rank::BaseRankItem(); }
    rank::RankContext* CreateContext() const override { return new rank::RankContext(); }
    rank::Rank* CreateRank() const override { return new SugRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SUG_FACTORY_H
