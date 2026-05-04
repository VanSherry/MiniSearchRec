// ============================================================
// MiniSearchRec - 向量语义召回处理器实现（V1 阶段，DAG 并行模式）
// 基于 Embedding 的语义召回
// ============================================================

#include "lib/recall/vector_recall.h"
#include "framework/app_context.h"
#include "utils/logger.h"

namespace minisearchrec {

int VectorRecallProcessor::Init(const YAML::Node& config) {
    if (config["enable"]) {
        enabled_ = config["enable"].as<bool>(false);  // 默认关闭
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

int VectorRecallProcessor::ProcessDag(framework::DagProcessorContext* ctx) {
    if (!enabled_) return 0;

    // 延迟初始化 VectorIndex（Init 时 AppContext 可能未就绪）
    if (!vec_idx_) {
        vec_idx_ = AppContext::Instance().GetVectorIndex();
        if (!vec_idx_) {
            return 0;
        }
    }

    auto* ss = dynamic_cast<const SearchSession*>(ctx->session);
    if (!ss) return -1;

    if (ss->qp_info.query_embedding.empty()) {
        return 0;
    }

    auto results = vec_idx_->Search(
        ss->qp_info.query_embedding,
        top_k_,
        similarity_threshold_
    );

    std::vector<DocCandidate> candidates;

    int count = 0;
    for (const auto& [doc_id, sim] : results) {
        if (count >= max_recall_) break;

        DocCandidate cand;
        cand.doc_id = doc_id;
        cand.recall_source = "vector";
        cand.recall_score = sim;

        ctx->output->doc_scores.emplace_back(doc_id, sim);
        candidates.push_back(std::move(cand));
        count++;
    }

    ctx->output->items = std::move(candidates);
    ctx->output->item_count = count;

    LOG_INFO("VectorRecall: recalled {} docs (top_k={}, threshold={:.2f})",
             count, top_k_, similarity_threshold_);
    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(VectorRecallProcessor);
