// ============================================================
// MiniSearchRec - Hint 排序上下文
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_HINT_CONTEXT_H
#define MINISEARCHREC_HINT_CONTEXT_H

#include "lib/rank/base/rank_context.h"
#include <string>
#include <vector>

namespace minisearchrec {

class HintContext : public rank::RankContext {
public:
    int Init(const rank::RankArgs& args) override {
        int ret = RankContext::Init(args);
        if (ret != 0) return ret;
        doc_id = args.doc_id;
        query = args.query;
        return 0;
    }
    ~HintContext() override = default;

    // ── Hint 专用上下文 ──
    std::string doc_id;           // 用户点击的源文档 ID
    std::string query;            // 用户当前搜索词（可能为空）

    // 源文档信息（PrepareInput 阶段填充）
    std::string src_title;
    std::string src_category;
    std::vector<std::string> src_tags;
    int64_t src_click_count = 0;

    // 排序参数
    float tag_overlap_weight = 0.4f;
    float cooccur_weight = 0.4f;
    float freshness_weight = 0.2f;
    int dedup_edit_distance = 3;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HINT_CONTEXT_H
