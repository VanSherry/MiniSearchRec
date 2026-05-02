// ============================================================
// MiniSearchRec - Sug Factory + SugRank 实现
// ============================================================

#include "biz/sug/sug_factory.h"
#include "biz/sug/sug_trie.h"
#include "lib/rank/base/rank_factory.h"
#include "utils/logger.h"
#include <cmath>
#include <ctime>

namespace minisearchrec {

// ── SugRank::PrepareInput ──
// 从 Trie 前缀树召回候选词，填入 RankVector
int SugRank::PrepareInput() {
    auto sug_ctx = std::static_pointer_cast<SugContext>(ctx_);
    auto vec = ctx_->GetVector();

    if (sug_ctx->prefix.empty()) {
        LOG_WARN("SugRank::PrepareInput: empty prefix");
        return 0;
    }

    // 从 Trie 召回
    auto& trie = SugTrie::Instance();
    auto candidates = trie.Search(sug_ctx->prefix, 50);

    if (candidates.empty()) {
        LOG_INFO("SugRank::PrepareInput: no trie results for prefix='{}'", sug_ctx->prefix);
        return 0;
    }

    // 计算全局 max_freq（用于归一化）
    float max_freq = 1.0f;
    for (const auto* entry : candidates) {
        max_freq = std::max(max_freq, static_cast<float>(entry->freq));
    }
    sug_ctx->max_freq = max_freq;

    // 构建 RankItem 并填入 Vector
    int64_t now = std::time(nullptr);
    for (const auto* entry : candidates) {
        auto item = std::make_shared<SugRankItem>();
        item->SetWord(entry->word);
        item->source = entry->source;
        item->source_weight = entry->source_weight;
        item->freq = entry->freq;
        item->last_time = entry->last_time;
        item->SetType(0);  // sug type

        // 预计算基础特征
        float prefix_len = static_cast<float>(sug_ctx->prefix.size());
        float word_len = std::max(1.0f, static_cast<float>(entry->word.size()));
        item->prefix_match_ratio = prefix_len / word_len;

        item->freq_norm = std::log1p(static_cast<float>(entry->freq)) /
                         std::log1p(max_freq);

        float days = static_cast<float>(now - entry->last_time) / 86400.0f;
        item->freshness = std::exp(-0.099f * std::max(0.0f, days));

        vec->PushBack(item);
    }

    LOG_INFO("SugRank::PrepareInput: prefix='{}', candidates={}", sug_ctx->prefix, vec->Size());
    return 0;
}

// ── 注册 SugFactory ──
REGISTER_RANK_FACTORY(SugFactory);

} // namespace minisearchrec
