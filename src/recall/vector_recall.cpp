// ============================================================
// MiniSearchRec - 向量语义召回处理器实现（V1 阶段）
// 基于 Embedding 的语义召回
// ============================================================

#include "recall/vector_recall.h"
#include <iostream>

namespace minisearchrec {

bool VectorRecallProcessor::Init(const YAML::Node& config) {
    if (config["enable"]) {
        enabled_ = config["enable"].as<bool>(false);  // 默认关闭，V1 开启
    }
    if (config["max_recall"]) {
        max_recall_ = config["max_recall"].as<int>(200);
    }
    if (config["top_k"]) {
        top_k_ = config["top_k"].as<int>(200);
    }
    if (config["similarity_threshold"]) {
        similarity_threshold_ = config["similarity_threshold"].as<float>(0.7f);
    }
    return true;
}

int VectorRecallProcessor::Process(Session& session) {
    if (!enabled_) return 0;
    if (!vec_idx_) {
        std::cerr << "[VectorRecall] VectorIndex not set\n";
        return -1;
    }
    if (session.qp_info.query_embedding.empty()) {
        // Query 尚未向量化，跳过
        return 0;
    }

    auto results = vec_idx_->Search(
        session.qp_info.query_embedding,
        top_k_,
        similarity_threshold_
    );

    int count = 0;
    for (const auto& [doc_id, sim] : results) {
        if (count >= max_recall_) break;

        // 检查是否已在召回结果中
        bool exists = false;
        for (const auto& cand : session.recall_results) {
            if (cand.doc_id == doc_id) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            DocCandidate cand;
            cand.doc_id = doc_id;
            cand.recall_source = "vector";
            cand.recall_score = sim;  // 相似度作为召回分
            session.recall_results.push_back(cand);
            count++;
        }
    }

    session.counts.recall_source_counts["vector"] = count;
    return 0;
}

} // namespace minisearchrec
