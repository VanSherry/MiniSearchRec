// ============================================================
// MiniSearchRec - Sug 排序上下文
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_SUG_CONTEXT_H
#define MINISEARCHREC_SUG_CONTEXT_H

#include "lib/rank/base/rank_context.h"
#include <string>

namespace minisearchrec {

class SugContext : public rank::RankContext {
public:
    int Init(const rank::RankArgs& args) override {
        int ret = RankContext::Init(args);
        if (ret != 0) return ret;
        prefix = args.query;  // sug 的 query 就是前缀
        return 0;
    }

    ~SugContext() override = default;

    // ── Sug 专用上下文 ──
    std::string prefix;   // 用户输入的前缀

    // 排序权重（从配置读取）
    float prefix_match_weight = 0.5f;
    float freq_weight = 0.3f;
    float freshness_weight = 0.2f;

    // 过滤参数
    int min_word_len = 2;
    int max_word_len = 20;
    int dedup_edit_distance = 2;

    // 全局统计（PrepareInput 阶段计算）
    float max_freq = 1.0f;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SUG_CONTEXT_H
