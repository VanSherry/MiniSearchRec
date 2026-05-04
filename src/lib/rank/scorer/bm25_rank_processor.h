#ifndef MINISEARCHREC_BM25_RANK_PROCESSOR_H
#define MINISEARCHREC_BM25_RANK_PROCESSOR_H

#include "lib/rank/engine/rank_engine.h"

namespace minisearchrec {

class BM25RankProcessor : public rank::ProcessorInterface {
public:
    int Init(const rank::ProcessorConfig* config) override;
    int Process(std::vector<rank::RankItem>& items) override;
    std::string Name() const override { return "BM25RankProcessor"; }
};

} // namespace minisearchrec

#endif
