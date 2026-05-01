// ============================================================
// MiniSearchRec - 倒排索引召回处理器
// 对应微信搜推的 InvertedIndexRecall
// ============================================================

#ifndef MINISEARCHREC_INVERTED_RECALL_H
#define MINISEARCHREC_INVERTED_RECALL_H

#include "core/processor.h"
#include "index/inverted_index.h"

namespace minisearchrec {

class InvertedRecallProcessor : public BaseRecallProcessor {
public:
    InvertedRecallProcessor() = default;
    ~InvertedRecallProcessor() override = default;

    int Process(Session& session) override;
    std::string Name() const override { return "InvertedRecallProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    int max_recall_ = 1000;
    int min_term_freq_ = 1;
    std::shared_ptr<InvertedIndex> index_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_INVERTED_RECALL_H
