#ifndef MINISEARCHREC_SEARCH_FACTORY_H
#define MINISEARCHREC_SEARCH_FACTORY_H

#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_factory.h"
#include "biz/search/search_rank_item.h"
#include "biz/search/search_context.h"

namespace minisearchrec {

// ============================================================
// SearchRank：搜索排序 Pipeline
// PrepareInput: DocCandidate → SearchRankItem
// GenerateRankOutput: SearchRankItem → DocCandidate
// ============================================================
class SearchRank : public rank::Rank {
public:
    std::string RankName() const override { return "SearchRank"; }

protected:
    int PrepareInput() override;
    int GenerateRankOutput() override;
};

// ============================================================
// SearchFactory：搜索排序工厂
// ============================================================
class SearchFactory : public rank::RankFactory {
public:
    rank::BaseRankItem* CreateItem() const override { return new SearchRankItem(); }
    rank::RankContext* CreateContext() const override { return new SearchContext(); }
    rank::Rank* CreateRank() const override { return new SearchRank(); }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SEARCH_FACTORY_H
