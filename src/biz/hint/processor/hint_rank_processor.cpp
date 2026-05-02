// ============================================================
// MiniSearchRec - Hint 排序 Processor 实现
// 对标：ClickHintBaseDeepFMRankProcessor (特征组装+线性模型)
//       + PostRankProcessor (去重/MMR/截断)
// ============================================================

#include "biz/hint/processor/hint_rank_processor.h"
#include "biz/hint/hint_rank_item.h"
#include "biz/hint/hint_context.h"
#include "utils/string_utils.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace minisearchrec {

// ============================================================
// HintScoreProcessor：规则打分（模拟 DeepFM 的线性部分）
// 特征融合：recall_score × w1 + tag_overlap × w2 + cooccur × w3 + freshness × w4
// ============================================================
int HintScoreProcessor::Process() {
    auto hint_ctx = std::static_pointer_cast<HintContext>(ctx_);
    auto vec = ctx_->GetVector();

    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = static_cast<HintRankItem*>(vec->GetItem(i).get());

        // 规则特征融合（对标 DeepFM 的浅层线性部分）
        float score = 0.0f;

        // 召回分（不同来源有不同 baseline）
        score += item->recall_score * 0.3f;

        // 标签重叠度加权
        score += item->tag_overlap * hint_ctx->tag_overlap_weight;

        // 共现分加权
        score += item->cooccur_score * hint_ctx->cooccur_weight * 0.3f;

        // 分类热门分
        score += item->category_score * 0.05f;

        // 多路召回 boost（来自多个来源的 item 更可信）
        if (item->RetrieveTypes().size() > 1) {
            score *= 1.2f;
        }

        item->model_score = score;
        item->SetScore(score);
    }

    // 按分数排序
    vec->SortByScore();
    return 0;
}

// ============================================================
// HintPostRankProcessor：去重 + 长度过滤 + 截断
// 对标：PostRankProcessor::Rank4Dedupe + Rank4QualityFilter
// ============================================================
int HintPostRankProcessor::Process() {
    auto hint_ctx = std::static_pointer_cast<HintContext>(ctx_);
    auto vec = ctx_->GetVector();

    if (vec->Empty()) return 0;

    // ── Step 1: 长度过滤 ──
    for (uint32_t i = 0; i < vec->Size(); ) {
        auto* item = static_cast<HintRankItem*>(vec->GetItem(i).get());
        auto len = utils::Utf8Len(item->Word());
        if (len < 2 || len > 30) {
            vec->SetItemFilter(i, "length_filter");
        } else {
            ++i;
        }
    }

    // ── Step 2: 编辑距离去重（对标 Rank4DedupeNew）──
    int dedup_threshold = hint_ctx->dedup_edit_distance;
    std::vector<std::string> kept_words;

    for (uint32_t i = 0; i < vec->Size(); ) {
        auto* item = static_cast<HintRankItem*>(vec->GetItem(i).get());
        bool is_dup = false;

        for (const auto& kept : kept_words) {
            int ed = EditDistance(item->Word(), kept);
            if (ed < dedup_threshold) {
                is_dup = true;
                break;
            }
        }

        if (is_dup) {
            vec->SetItemFilter(i, "dedup");
        } else {
            kept_words.push_back(item->Word());
            ++i;
        }
    }

    // ── Step 3: 计算 final_score ──
    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = static_cast<HintRankItem*>(vec->GetItem(i).get());
        item->final_score = item->Score();
    }

    // ── Step 4: 截断到 page_size ──
    vec->Truncate(hint_ctx->PageSize());

    return 0;
}

int HintPostRankProcessor::EditDistance(const std::string& a, const std::string& b) {
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

REGISTER_RANK_PROCESSOR(HintScoreProcessor);
REGISTER_RANK_PROCESSOR(HintPostRankProcessor);

} // namespace minisearchrec
