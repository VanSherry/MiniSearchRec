// ============================================================
// MiniSearchRec - 向量语义召回处理器（V1 阶段）
// 基于 Embedding 的语义召回
// ============================================================

#ifndef MINISEARCHREC_VECTOR_RECALL_H
#define MINISEARCHREC_VECTOR_RECALL_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"
#include "lib/index/vector_index.h"

namespace minisearchrec {

class VectorRecallProcessor : public BaseRecallProcessor {
public:
    VectorRecallProcessor() = default;
    ~VectorRecallProcessor() override = default;

    int Process(Session& session) override;
    std::string Name() const override { return "VectorRecallProcessor"; }
    bool Init(const YAML::Node& config) override;

    void SetVectorIndex(std::shared_ptr<VectorIndex> idx) {
        vec_idx_ = idx;
    }

private:
    int max_recall_ = 200;
    int top_k_ = 200;
    float similarity_threshold_ = 0.7f;
    std::shared_ptr<VectorIndex> vec_idx_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_VECTOR_RECALL_H
