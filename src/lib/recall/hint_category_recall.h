#ifndef MINISEARCHREC_HINT_CATEGORY_RECALL_H
#define MINISEARCHREC_HINT_CATEGORY_RECALL_H

#include "framework/processor/dag_pipeline.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class HintCategoryRecall : public BaseRecallProcessor {
public:
    int ProcessDag(framework::DagProcessorContext* ctx) override;
    std::string Name() const override { return "HintCategoryRecall"; }
    int Init(const YAML::Node& config) override { return 0; }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HINT_CATEGORY_RECALL_H
