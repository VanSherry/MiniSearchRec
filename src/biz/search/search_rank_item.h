#ifndef MINISEARCHREC_SEARCH_RANK_ITEM_H
#define MINISEARCHREC_SEARCH_RANK_ITEM_H

#include "lib/rank/base/rank_item.h"

namespace minisearchrec {

// 搜索业务排序条目
// 所有分数通过 BaseRankItem::features_ KV 存储
// 硬编码字段只保留 cand_index（用于 GenerateRankOutput 写回 DocCandidate）
class SearchRankItem : public rank::BaseRankItem {
public:
    int cand_index = -1;   // 在 recall_results/coarse_rank_results 中的下标
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SEARCH_RANK_ITEM_H
