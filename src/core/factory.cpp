// ============================================================
// MiniSearchRec - 处理器工厂实现
// ============================================================

#include "core/factory.h"
#include "core/processor.h"

// 召回处理器
#include "recall/inverted_recall.h"
#include "recall/user_history_recall.h"
#include "recall/hot_content_recall.h"
// #include "recall/vector_recall.h"  // V1 阶段开启

// 打分处理器
#include "rank/bm25_scorer.h"
#include "rank/quality_scorer.h"
#include "rank/freshness_scorer.h"
// #include "rank/lgbm_ranker.h"  // V1 阶段开启

// 过滤处理器
#include "filter/dedup_filter.h"
#include "filter/quality_filter.h"
// #include "filter/spam_filter.h"
// #include "filter/blacklist_filter.h"

// 后处理处理器
#include "rank/mmr_reranker.h"

namespace minisearchrec {

void RegisterBuiltinProcessors() {
    // 召回处理器
    REGISTER_RECALL(InvertedRecallProcessor);
    REGISTER_RECALL(UserHistoryRecallProcessor);
    REGISTER_RECALL(HotContentRecallProcessor);
    // REGISTER_RECALL(VectorRecallProcessor);  // V1 阶段开启

    // 打分处理器（粗排）
    REGISTER_SCORER(BM25ScorerProcessor);
    REGISTER_SCORER(QualityScorerProcessor);
    REGISTER_SCORER(FreshnessScorerProcessor);

    // 打分处理器（精排）
    // REGISTER_SCORER(LGBMScorerProcessor);  // V1 阶段开启

    // 过滤处理器
    REGISTER_FILTER(DedupFilterProcessor);
    REGISTER_FILTER(QualityFilterProcessor);
    // REGISTER_FILTER(SpamFilterProcessor);
    // REGISTER_FILTER(BlacklistFilterProcessor);

    // 后处理处理器
    REGISTER_POSTPROCESS(MMRRerankProcessor);
}

} // namespace minisearchrec
