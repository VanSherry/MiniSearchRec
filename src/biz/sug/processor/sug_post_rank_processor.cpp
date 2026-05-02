// ============================================================
// MiniSearchRec - Sug 后排序 Processor 实现
// 对标：PostModelRankProcessor（融合 QV/转移概率/CTR/相关性）
//       + DynamicDeduplicationProcessor（编辑距离去重）
// ============================================================

#include "biz/sug/processor/sug_post_rank_processor.h"
#include "biz/sug/sug_rank_item.h"
#include "biz/sug/sug_context.h"
#include "utils/string_utils.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace minisearchrec {

int SugPostRankProcessor::Process() {
    auto sug_ctx = std::static_pointer_cast<SugContext>(ctx_);
    auto vec = ctx_->GetVector();

    if (vec->Empty()) return 0;

    // ── Step 1: 最终分数融合 ──
    // 对标 PostModelRankProcessor 的多特征融合：
    // final_score = relevance * w1 + freq_norm * w2 + freshness * w3
    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = static_cast<SugRankItem*>(vec->GetItem(i).get());

        item->final_score =
            item->relevance_score * sug_ctx->prefix_match_weight +
            item->freq_norm * sug_ctx->freq_weight +
            item->freshness * sug_ctx->freshness_weight;

        // 来源权重加成（title > user_query > tag）
        item->final_score *= item->source_weight;

        item->SetScore(item->final_score);
    }

    // ── Step 2: 按 final_score 排序 ──
    vec->SortByScore();

    // ── Step 3: 编辑距离去重（对标 DynamicDeduplicationProcessor）──
    // 保留高分词，移除与已保留词编辑距离 < threshold 的低分词
    int dedup_threshold = sug_ctx->dedup_edit_distance;
    std::vector<std::string> kept_words;

    for (uint32_t i = 0; i < vec->Size(); ) {
        auto* item = static_cast<SugRankItem*>(vec->GetItem(i).get());
        bool is_dup = false;

        for (const auto& kept : kept_words) {
            if (EditDistance(item->Word(), kept) < dedup_threshold) {
                is_dup = true;
                break;
            }
        }

        if (is_dup) {
            vec->SetItemFilter(i, "dedup_edit_distance");
        } else {
            kept_words.push_back(item->Word());
            ++i;
        }
    }

    // ── Step 4: 截断到 page_size ──
    vec->Truncate(sug_ctx->PageSize());

    return 0;
}

int SugPostRankProcessor::EditDistance(const std::string& a, const std::string& b) {
    int m = a.size(), n = b.size();
    if (m == 0) return n;
    if (n == 0) return m;
    std::vector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;
    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            curr[j] = std::min({prev[j]+1, curr[j-1]+1, prev[j-1]+cost});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

REGISTER_RANK_PROCESSOR(SugPostRankProcessor);

} // namespace minisearchrec
