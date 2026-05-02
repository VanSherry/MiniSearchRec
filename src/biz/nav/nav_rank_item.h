// ============================================================
// MiniSearchRec - Nav 排序元素
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_NAV_RANK_ITEM_H
#define MINISEARCHREC_NAV_RANK_ITEM_H

#include "lib/rank/base/rank_item.h"
#include <string>

namespace minisearchrec {

struct NavRankItem : public rank::BaseRankItem {
    std::string source;           // hot / preset / user_history / category_hot
    float hot_score = 0.0f;       // 热度衰减分
    float freshness = 0.0f;
    float model_score = 0.0f;     // Adams 简化评分
    float final_score = 0.0f;
    int64_t click_count = 0;
    int64_t publish_time = 0;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_NAV_RANK_ITEM_H
