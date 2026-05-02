// ============================================================
// MiniSearchRec - BM25 打分器实现
// 参考：业界粗排打分方案（BM25 + Light Ranker）
// ============================================================

#include "lib/rank/scorer/bm25_scorer.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include <cmath>
#include <algorithm>

namespace minisearchrec {

int BM25ScorerProcessor::Init(const YAML::Node& config) {
    if (config["weight"]) {
        weight_ = config["weight"].as<float>(1.0f);
    }
    if (config["k1"]) {
        k1_ = config["k1"].as<float>(1.5f);
    }
    if (config["b"]) {
        b_ = config["b"].as<float>(0.75f);
    }
    // 从全局上下文获取倒排索引
    index_ = AppContext::Instance().GetInvertedIndex();
    if (!index_) {
        LOG_WARN("BM25ScorerProcessor: InvertedIndex not ready in AppContext");
    }
    return 0;
}

float BM25ScorerProcessor::CalculateBM25(float tf,
                                          float idf,
                                          float doc_len,
                                          float avg_doc_len,
                                          float k1,
                                          float b) {
    if (avg_doc_len < 1e-6f) return 0.0f;

    float norm_len = doc_len / avg_doc_len;
    float numerator = tf * (k1 + 1.0f);
    float denominator = tf + k1 * (1.0f - b + b * norm_len);

    return idf * (numerator / denominator);
}

int BM25ScorerProcessor::Process(Session& session,
                                  std::vector<DocCandidate>& candidates) {
    if (candidates.empty()) return 0;

    // 延迟获取索引（应对初始化顺序）
    if (!index_) {
        index_ = AppContext::Instance().GetInvertedIndex();
    }
    float avg_doc_len = index_ ? index_->GetAvgDocLen() : 500.0f;

    const auto& terms = session.qp_info.terms;

    for (auto& cand : candidates) {
        float bm25_score = 0.0f;

        // 获取该文档的所有倒排信息
        auto postings = index_ ? index_->GetDocPostings(cand.doc_id)
                               : std::unordered_map<std::string, PostingNode>{};

        for (const auto& term : terms) {
            auto it = postings.find(term);
            if (it == postings.end()) continue;

            float tf = static_cast<float>(it->second.term_freq);
            float field_weight = it->second.field_weight;
            float idf = session.qp_info.term_idf.count(term)
                            ? session.qp_info.term_idf.at(term)
                            : (index_ ? index_->CalculateIDF(term) : 1.0f);

            float single_score = CalculateBM25(tf, idf,
                                               static_cast<float>(it->second.doc_len),
                                               avg_doc_len,
                                               k1_, b_);
            bm25_score += single_score * field_weight;
        }

        // 归一化到 [0, 1]：10.0f 为经验缩放系数，tanh 将任意正分数压缩到 (0,1)
        constexpr float kBM25NormScale = 10.0f;
        bm25_score = std::tanh(bm25_score / kBM25NormScale);

        cand.coarse_score += bm25_score * weight_;
        cand.debug_scores["bm25"] = bm25_score;
    }

    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(BM25ScorerProcessor);
