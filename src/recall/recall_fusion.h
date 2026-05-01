// ============================================================
// MiniSearchRec - 多路召回融合
// 参考：微信 Miner 平台，使用 RRF 算法
// RRF: score(d) = Σ 1/(k + rank_i(d))
// ============================================================

#ifndef MINISEARCHREC_RECALL_FUSION_H
#define MINISEARCHREC_RECALL_FUSION_H

#include "core/session.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace minisearchrec {

// 召回融合器（静态工具类）
class RecallFusion {
public:
    // RRF (Reciprocal Rank Fusion) 融合
    // multi_results: 每个召回源的结果列表（已按相关度排序）
    // max_total: 融合后保留的候选总数
    // k: RRF 参数，默认 60
    static std::vector<DocCandidate> FuseByRRF(
        const std::vector<std::vector<DocCandidate>>& multi_results,
        int max_total = 1500,
        int k = 60
    );

    // 加权平均融合（简单版）
    static std::vector<DocCandidate> FuseByWeightedAvg(
        const std::vector<std::vector<DocCandidate>>& multi_results,
        const std::vector<float>& weights,
        int max_total = 1500
    );
};

} // namespace minisearchrec

#endif // MINISEARCHREC_RECALL_FUSION_H
