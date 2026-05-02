// ============================================================
// MiniSearchRec - Nav 评分 Processor 实现
// 对标：QXNavRank + Adams 模型简化
// ============================================================

#include "biz/nav/processor/nav_score_processor.h"
#include "biz/nav/nav_rank_item.h"
#include "biz/nav/nav_context.h"
#include "utils/string_utils.h"
#include <algorithm>
#include <unordered_set>

namespace minisearchrec {

int NavScoreProcessor::Process() {
    auto nav_ctx = std::static_pointer_cast<NavContext>(ctx_);
    auto vec = ctx_->GetVector();

    if (vec->Empty()) return 0;

    // ── Step 1: 长度过滤 ──
    for (uint32_t i = 0; i < vec->Size(); ) {
        auto* item = static_cast<NavRankItem*>(vec->GetItem(i).get());
        auto len = utils::Utf8Len(item->Word());
        if (len < 2 || len > 20) {
            vec->SetItemFilter(i, "length_filter");
        } else {
            ++i;
        }
    }

    // ── Step 2: 按 hot_score 排序（已在 PrepareInput 中设置 Score）──
    vec->SortByScore();

    // ── Step 3: 去重（完全相同的词）──
    std::unordered_set<std::string> seen;
    for (uint32_t i = 0; i < vec->Size(); ) {
        auto* item = static_cast<NavRankItem*>(vec->GetItem(i).get());
        if (seen.count(item->Word())) {
            vec->SetItemFilter(i, "dedup");
        } else {
            seen.insert(item->Word());
            item->final_score = item->hot_score;
            ++i;
        }
    }

    // ── Step 4: 截断 ──
    vec->Truncate(nav_ctx->PageSize());

    return 0;
}

REGISTER_RANK_PROCESSOR(NavScoreProcessor);

} // namespace minisearchrec
