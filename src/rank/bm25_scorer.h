// ============================================================
// MiniSearchRec - BM25 打分器
// 工业标准文本相关性算法
// 参考：微信 L1/L2 打分、X(Twitter) Earlybird Light Ranker
// ============================================================

#ifndef MINISEARCHREC_BM25_SCORER_H
#define MINISEARCHREC_BM25_SCORER_H

#include "core/processor.h"
#include "index/inverted_index.h"

namespace minisearchrec {

class BM25ScorerProcessor : public BaseScorerProcessor {
public:
    BM25ScorerProcessor() = default;
    ~BM25ScorerProcessor() override = default;

    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override;

    std::string Name() const override { return "BM25ScorerProcessor"; }
    bool Init(const YAML::Node& config) override;

    // BM25 打分（静态方法，便于单元测试）
    static float CalculateBM25(float tf,
                                float idf,
                                float doc_len,
                                float avg_doc_len,
                                float k1 = 1.5f,
                                float b = 0.75f);

private:
    float k1_ = 1.5f;
    float b_ = 0.75f;
    std::shared_ptr<InvertedIndex> index_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_BM25_SCORER_H
