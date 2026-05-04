#ifndef MINISEARCHREC_NAV_GLOBAL_HOT_RECALL_H
#define MINISEARCHREC_NAV_GLOBAL_HOT_RECALL_H

#include "framework/processor/dag_pipeline.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class NavGlobalHotRecall : public BaseRecallProcessor {
public:
    int ProcessDag(framework::DagProcessorContext* ctx) override;
    std::string Name() const override { return "NavGlobalHotRecall"; }
    int Init(const YAML::Node& config) override { return 0; }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_NAV_GLOBAL_HOT_RECALL_H
