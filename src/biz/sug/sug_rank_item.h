// ============================================================
// MiniSearchRec - Sug 排序元素
// 对标：通用搜索框架 SuggesterRankItem
// ============================================================

#ifndef MINISEARCHREC_SUG_RANK_ITEM_H
#define MINISEARCHREC_SUG_RANK_ITEM_H

#include "lib/rank/base/rank_item.h"
#include <string>

namespace minisearchrec {

struct SugRankItem : public rank::BaseRankItem {
    // ── Sug 专用特征 ──
    std::string source;           // title / tag / user_query
    float source_weight = 1.0f;

    // 前缀匹配特征
    float prefix_match_ratio = 0.0f;  // prefix_len / word_len
    float cover_ratio = 0.0f;         // query chars covered
    float edit_distance_norm = 0.0f;  // normalized edit distance

    // 后验统计特征
    int64_t freq = 0;
    int64_t last_time = 0;
    float freq_norm = 0.0f;       // log(freq+1) / log(max_freq+1)
    float freshness = 0.0f;       // exp(-decay * days)

    // 文本相关性融合分
    float relevance_score = 0.0f;

    // 最终排序分
    float final_score = 0.0f;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SUG_RANK_ITEM_H
