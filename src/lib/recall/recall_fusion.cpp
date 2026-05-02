// ============================================================
// MiniSearchRec - 多路召回融合实现
// 参考：Reciprocal Rank Fusion (RRF) 多路融合算法
// ============================================================

#include "lib/recall/recall_fusion.h"
#include <algorithm>
#include <unordered_map>

namespace minisearchrec {

std::vector<DocCandidate> RecallFusion::FuseByRRF(
    const std::vector<std::vector<DocCandidate>>& multi_results,
    int max_total,
    int k
) {
    // fused_scores: doc_id -> (融合分数, 召回来源列表)
    std::unordered_map<std::string, std::pair<float, std::string>> fused_scores;

    for (const auto& results : multi_results) {
        for (int rank = 0; rank < (int)results.size(); ++rank) {
            const auto& cand = results[rank];
            float rrf_score = 1.0f / (k + rank + 1);

            auto it = fused_scores.find(cand.doc_id);
            if (it == fused_scores.end()) {
                std::string sources = cand.recall_source.empty()
                                         ? "unknown"
                                         : cand.recall_source;
                fused_scores[cand.doc_id] = {rrf_score, sources};
            } else {
                it->second.first += rrf_score;
                if (!cand.recall_source.empty()) {
                    it->second.second += " " + cand.recall_source;
                }
            }
        }
    }

    // 转换为向量并排序
    std::vector<DocCandidate> fused;
    fused.reserve(fused_scores.size());

    for (const auto& [doc_id, score_source] : fused_scores) {
        DocCandidate cand;
        cand.doc_id = doc_id;
        cand.recall_score = score_source.first;
        cand.recall_source = score_source.second;
        fused.push_back(cand);
    }

    std::sort(fused.begin(), fused.end(),
              [](const DocCandidate& a, const DocCandidate& b) {
                  return a.recall_score > b.recall_score;
              });

    if ((int)fused.size() > max_total) {
        fused.resize(max_total);
    }

    return fused;
}

std::vector<DocCandidate> RecallFusion::FuseByWeightedAvg(
    const std::vector<std::vector<DocCandidate>>& multi_results,
    const std::vector<float>& weights,
    int max_total
) {
    if (multi_results.empty()) return {};

    std::unordered_map<std::string, float> fused_scores;
    std::unordered_map<std::string, std::string> sources;

    for (size_t i = 0; i < multi_results.size(); ++i) {
        float weight = (i < weights.size()) ? weights[i] : 1.0f;

        // 归一化该路结果到 [0, 1]
        float max_score = 0.0f;
        for (const auto& cand : multi_results[i]) {
            max_score = std::max(max_score, cand.recall_score);
        }
        if (max_score < 1e-6f) max_score = 1.0f;

        for (const auto& cand : multi_results[i]) {
            float normalized = cand.recall_score / max_score;
            fused_scores[cand.doc_id] += weight * normalized;
            if (!cand.recall_source.empty()) {
                sources[cand.doc_id] = cand.recall_source;
            }
        }
    }

    std::vector<DocCandidate> fused;
    fused.reserve(fused_scores.size());

    for (const auto& [doc_id, score] : fused_scores) {
        DocCandidate cand;
        cand.doc_id = doc_id;
        cand.recall_score = score;
        auto it = sources.find(doc_id);
        cand.recall_source = (it != sources.end()) ? it->second : "fused";
        fused.push_back(cand);
    }

    std::sort(fused.begin(), fused.end(),
              [](const DocCandidate& a, const DocCandidate& b) {
                  return a.recall_score > b.recall_score;
              });

    if ((int)fused.size() > max_total) {
        fused.resize(max_total);
    }

    return fused;
}

} // namespace minisearchrec
