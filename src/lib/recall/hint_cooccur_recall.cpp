#include "lib/recall/hint_cooccur_recall.h"
#include "framework/app_context.h"
#include "lib/storage/doc_cooccur_store.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"

namespace minisearchrec {

int HintCooccurRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    if (!ctx->session) return 0;
    std::string doc_id = ctx->session->Get("doc_id");
    if (doc_id.empty()) { LOG_WARN("HintCooccurRecall: missing doc_id"); return 0; }

    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) { LOG_ERROR("HintCooccurRecall: DocStore not available"); return -1; }

    std::vector<DocCandidate> docs;
    float max_score = 1.0f;

    for (const auto& ci : DocCooccurStore::Instance().GetTopCooccur(doc_id, 20)) {
        Document doc;
        if (!doc_store->GetDoc(ci.dst_doc_id, doc) || doc.title().empty()) continue;

        DocCandidate cand;
        cand.doc_id = doc.title();
        cand.recall_source = "cooccur";
        cand.recall_score = 0.0f;
        float score = std::max(0.0f, (float)ci.co_count);
        cand.debug_scores["cooccur_score"] = score;
        max_score = std::max(max_score, score);
        ctx->output->doc_scores.emplace_back(doc.title(), 0.0f);
        docs.push_back(std::move(cand));
    }

    for (auto& d : docs) {
        d.debug_scores["cooccur_score"] /= max_score;
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("HintCooccurRecall: doc_id='{}', results={}", doc_id, ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(HintCooccurRecall);
