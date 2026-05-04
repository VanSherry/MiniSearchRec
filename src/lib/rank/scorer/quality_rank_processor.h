#ifndef MINISEARCHREC_QUALITY_RANK_PROCESSOR_H
#define MINISEARCHREC_QUALITY_RANK_PROCESSOR_H

#include "lib/rank/engine/rank_engine.h"

namespace minisearchrec {

class QualityRankProcessor : public rank::ProcessorInterface {
public:
    int Init(const rank::ProcessorConfig* config) override;
    int Process(std::vector<rank::RankItem>& items) override;
    std::string Name() const override { return "QualityRankProcessor"; }
};

} // namespace minisearchrec

#endif
