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
    // fused_scores: doc_id -> (融合分数, 召回来源列表, 候选文档)
    struct FusedEntry {
        float score = 0.0f;
        std::string sources;
        DocCandidate cand;  // 保留文档字段
    };
    std::unordered_map<std::string, FusedEntry> fused_map;

    for (const auto& results : multi_results) {
        for (int rank = 0; rank < (int)results.size(); ++rank) {
            const auto& cand = results[rank];
            float rrf_score = 1.0f / (k + rank + 1);

            auto it = fused_map.find(cand.doc_id);
            if (it == fused_map.end()) {
                FusedEntry entry;
                entry.score = rrf_score;
                entry.sources = cand.recall_source.empty()
                                         ? "unknown"
                                         : cand.recall_source;
                entry.cand = cand;  // 保留完整字段
                fused_map[cand.doc_id] = std::move(entry);
            } else {
                it->second.score += rrf_score;
                if (!cand.recall_source.empty()) {
                    it->second.sources += " " + cand.recall_source;
                }
                // 如果当前候选有更多信息（字段非空），则补充
                auto& existing = it->second.cand;
                if (existing.title.empty() && !cand.title.empty()) {
                    existing.title = cand.title;
                }
                if (existing.content_snippet.empty() && !cand.content_snippet.empty()) {
                    existing.content_snippet = cand.content_snippet;
                }
                if (existing.author.empty() && !cand.author.empty()) {
                    existing.author = cand.author;
                }
                if (existing.category.empty() && !cand.category.empty()) {
                    existing.category = cand.category;
                }
                if (existing.quality_score == 0.0f && cand.quality_score != 0.0f) {
                    existing.quality_score = cand.quality_score;
                }
                if (existing.click_count == 0 && cand.click_count != 0) {
                    existing.click_count = cand.click_count;
                }
                if (existing.like_count == 0 && cand.like_count != 0) {
                    existing.like_count = cand.like_count;
                }
                if (existing.publish_time == 0 && cand.publish_time != 0) {
                    existing.publish_time = cand.publish_time;
                }
            }
        }
    }

    // 转换为向量并排序
    std::vector<DocCandidate> fused;
    fused.reserve(fused_map.size());

    for (auto& [doc_id, entry] : fused_map) {
        entry.cand.recall_score = entry.score;
        entry.cand.recall_source = entry.sources;
        fused.push_back(std::move(entry.cand));
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
