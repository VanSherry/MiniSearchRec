// ============================================================
// MiniSearchRec - Sug 相关性评分 Processor 实现
// 对标：通用搜索排序服务
// ============================================================

#include "biz/sug/processor/sug_relevance_processor.h"
#include "biz/sug/sug_rank_item.h"
#include "biz/sug/sug_context.h"
#include "lib/rank/base/processor_interface.h"
#include "utils/string_utils.h"
#include <algorithm>
#include <cmath>

namespace minisearchrec {

int SugRelevanceProcessor::Process() {
    auto sug_ctx = std::static_pointer_cast<SugContext>(ctx_);
    auto vec = ctx_->GetVector();
    const std::string& prefix = sug_ctx->prefix;

    for (uint32_t i = 0; i < vec->Size(); ) {
        auto base_item = vec->GetItem(i);
        auto* item = static_cast<SugRankItem*>(base_item.get());

        // 1. 计算覆盖率
        item->cover_ratio = CoverRatio(prefix, item->Word());

        // 2. 计算编辑距离归一化（仅在前缀长度对齐时）
        std::string word_prefix = item->Word().substr(0, prefix.size());
        int ed = EditDistance(prefix, word_prefix);
        float max_len = std::max(prefix.size(), word_prefix.size());
        item->edit_distance_norm = max_len > 0 ? (1.0f - ed / max_len) : 0.0f;

        // 3. 融合相关性分
        // 对标 SugContext 中的权重：prefix_match + suffix + cover + edit_distance
        item->relevance_score =
            item->prefix_match_ratio * 0.3f +
            item->cover_ratio * 0.3f +
            item->edit_distance_norm * 0.2f +
            item->source_weight * 0.2f;

        // 4. 过滤：相关性太低（覆盖率 < 0.3 且 编辑距离归一化 < 0.3）
        if (item->cover_ratio < 0.2f && item->edit_distance_norm < 0.3f) {
            vec->SetItemFilter(i, "low_relevance");
            continue;  // SetItemFilter 已经移除了该 item，不需要 i++
        }

        // 5. 过滤过短/过长
        auto len = utils::Utf8Len(item->Word());
        if (len < (size_t)sug_ctx->min_word_len || len > (size_t)sug_ctx->max_word_len) {
            vec->SetItemFilter(i, "length_filter");
            continue;
        }

        ++i;
    }

    return 0;
}

int SugRelevanceProcessor::EditDistance(const std::string& a, const std::string& b) {
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

float SugRelevanceProcessor::CoverRatio(const std::string& query, const std::string& word) {
    if (query.empty()) return 0.0f;
    // 简单实现：query 中每个字节在 word 中出现的比例
    int hit = 0;
    for (char c : query) {
        if (word.find(c) != std::string::npos) ++hit;
    }
    return static_cast<float>(hit) / query.size();
}

REGISTER_RANK_PROCESSOR(SugRelevanceProcessor);

} // namespace minisearchrec
