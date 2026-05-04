#include "lib/recall/hint_category_recall.h"
#include "framework/app_context.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"
#include <unordered_set>

namespace minisearchrec {

int HintCategoryRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    if (!ctx->session) return 0;
    std::string doc_id = ctx->session->Get("doc_id");
    if (doc_id.empty()) { LOG_WARN("HintCategoryRecall: missing doc_id"); return 0; }

    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) { LOG_ERROR("HintCategoryRecall: DocStore not available"); return -1; }

    Document src_doc;
    if (!doc_store->GetDoc(doc_id, src_doc) || src_doc.category().empty()) return 0;

    std::unordered_set<std::string> seen;
    seen.insert(doc_id);

    std::vector<DocCandidate> docs;
    float max_click = 1.0f;

    for (const auto& id : doc_store->GetAllDocIds()) {
        if (seen.count(id)) continue;
        Document doc;
        if (!doc_store->GetDoc(id, doc)) continue;
        if (doc.category() == src_doc.category() && !doc.title().empty()) {
            DocCandidate cand;
            cand.doc_id = doc.title();
            cand.recall_source = "category_hot";
            cand.recall_score = 0.0f;
            cand.click_count = doc.click_count();
            max_click = std::max(max_click, (float)doc.click_count());
            ctx->output->doc_scores.emplace_back(doc.title(), 0.0f);
            docs.push_back(std::move(cand));
            seen.insert(id);
        }
    }

    for (auto& d : docs) {
        d.debug_scores["category_relevance"] = std::log1pf((float)d.click_count) / std::log1pf(max_click);
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("HintCategoryRecall: doc_id='{}', results={}", doc_id, ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(HintCategoryRecall);
