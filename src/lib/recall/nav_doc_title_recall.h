#ifndef MINISEARCHREC_NAV_DOC_TITLE_RECALL_H
#define MINISEARCHREC_NAV_DOC_TITLE_RECALL_H

#include "framework/processor/dag_pipeline.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class NavDocTitleRecall : public BaseRecallProcessor {
public:
    int ProcessDag(framework::DagProcessorContext* ctx) override;
    std::string Name() const override { return "NavDocTitleRecall"; }
    int Init(const YAML::Node& config) override { return 0; }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_NAV_DOC_TITLE_RECALL_H
