// ============================================================
// MiniSearchRec - 向量语义召回处理器实现（V1 阶段）
// 基于 Embedding 的语义召回
// ============================================================

#include "lib/recall/vector_recall.h"
#include <iostream>
#include <unordered_set>

namespace minisearchrec {

int VectorRecallProcessor::Init(const YAML::Node& config) {
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
    return 0;
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
    // 构建已有 doc_id 集合，O(1) 查找
    std::unordered_set<std::string> existing_ids;
    for (const auto& cand : session.recall_results) {
        existing_ids.insert(cand.doc_id);
    }

    for (const auto& [doc_id, sim] : results) {
        if (count >= max_recall_) break;

        if (existing_ids.count(doc_id) == 0) {
            DocCandidate cand;
            cand.doc_id = doc_id;
            cand.recall_source = "vector";
            cand.recall_score = sim;  // 相似度作为召回分
            session.recall_results.push_back(cand);
            existing_ids.insert(doc_id);
            count++;
        }
    }

    session.search_counts.recall_source_counts["vector"] = count;
    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(VectorRecallProcessor);
