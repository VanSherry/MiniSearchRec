#ifndef MINISEARCHREC_BM25_RANK_PROCESSOR_H
#define MINISEARCHREC_BM25_RANK_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

// BM25 文本相关性粗排 Processor（通用 KV 特征版）
// 读取 PrepareInput 预计算的 "bm25" 特征，加权累加到 "total_score"
class BM25RankProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "BM25RankProcessor"; }
};

} // namespace minisearchrec

#endif
