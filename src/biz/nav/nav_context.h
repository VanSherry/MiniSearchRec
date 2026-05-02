// ============================================================
// MiniSearchRec - Nav 排序上下文
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_NAV_CONTEXT_H
#define MINISEARCHREC_NAV_CONTEXT_H

#include "lib/rank/base/rank_context.h"
#include <vector>
#include <string>

namespace minisearchrec {

class NavContext : public rank::RankContext {
public:
    ~NavContext() override = default;

    // Nav 参数
    float decay_rate = 0.099f;    // ln(2)/7 ≈ 0.099 (7天半衰期)
    std::vector<std::string> preset_words = {
        "深度学习", "算法面试", "系统设计",
        "Redis", "分布式系统", "搜索推荐"
    };
};

} // namespace minisearchrec

#endif // MINISEARCHREC_NAV_CONTEXT_H
