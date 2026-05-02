// ============================================================
// MiniSearchRec - MMR 多样性重排
// 参考工业标准多样性算法
// MMR: score(d) = λ * relevance(d,q) - (1-λ) * max sim(d, d_j)
// ============================================================

#ifndef MINISEARCHREC_MMR_RERANKER_H
#define MINISEARCHREC_MMR_RERANKER_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class MMRRerankProcessor : public BasePostProcessProcessor {
public:
    MMRRerankProcessor() = default;
    ~MMRRerankProcessor() override = default;

    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override;

    std::string Name() const override { return "MMRRerankProcessor"; }
    int Init(const YAML::Node& config) override;

private:
    float lambda_ = 0.7f;       // 相关性 vs 多样性权衡（0.7偏相关性）
    int top_k_ = 20;            // 最终返回数量
    int max_candidates_ = 100;  // 参与重排的最大候选数

    // 计算两个文档的相似度（基于标题的简化版）
    static float CalcSimilarity(const DocCandidate& a,
                                const DocCandidate& b);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_MMR_RERANKER_H
