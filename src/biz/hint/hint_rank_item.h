// ============================================================
// MiniSearchRec - Hint 排序元素
// 对标：通用搜索排序服务
// ============================================================

#ifndef MINISEARCHREC_HINT_RANK_ITEM_H
#define MINISEARCHREC_HINT_RANK_ITEM_H

#include "lib/rank/base/rank_item.h"
#include <string>
#include <vector>
#include <set>

namespace minisearchrec {

struct HintRankItem : public rank::BaseRankItem {
    // ── 基本信息 ──
    std::string text;             // hint 词条文本
    std::string source;           // 召回来源：tag_match / category_hot / cooccur / query_expand
    std::set<uint32_t> recall_id_set;  // 多路召回来源 ID

    // ── 召回特征 ──
    float recall_score = 0.0f;    // 召回阶段的分数
    float tag_overlap = 0.0f;     // 与源文档的标签重叠度 (Jaccard)
    float cooccur_score = 0.0f;   // 行为共现分数
    float category_score = 0.0f;  // 分类热门分数

    // ── 排序特征 ──
    float relevance_score = 0.0f; // 文本相关性分（编辑距离/语义分）
    float quality_score = 0.0f;   // 质量分
    float freshness = 0.0f;       // 时效性
    float model_score = 0.0f;     // 模型打分（简化版 DeepFM 规则分）

    // ── 最终分 ──
    float final_score = 0.0f;

    // ── 去重标记 ──
    float char_edit_distance = 1.0f;
    bool dedupe_filter = false;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HINT_RANK_ITEM_H
