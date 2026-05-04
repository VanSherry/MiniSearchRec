#include "lib/recall/nav_doc_title_recall.h"
#include "framework/app_context.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"

namespace minisearchrec {

int NavDocTitleRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) { LOG_ERROR("NavDocTitleRecall: DocStore not available"); return -1; }

    std::vector<DocCandidate> docs;
    float max_click = 1.0f;

    for (const auto& id : doc_store->GetAllDocIds()) {
        Document doc;
        if (!doc_store->GetDoc(id, doc)) continue;
        if (doc.title().empty()) continue;

        DocCandidate cand;
        cand.doc_id = doc.title();
        cand.recall_source = "doc_hot";
        cand.recall_score = 0.0f;
        cand.click_count = doc.click_count();
        max_click = std::max(max_click, (float)doc.click_count());
        ctx->output->doc_scores.emplace_back(doc.title(), 0.0f);
        docs.push_back(std::move(cand));
    }

    for (auto& d : docs) {
        d.debug_scores["heat"] = std::log1pf((float)d.click_count) / std::log1pf(max_click);
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("NavDocTitleRecall: results={}", ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(NavDocTitleRecall);
