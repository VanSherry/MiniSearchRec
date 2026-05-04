#include "lib/recall/hint_tag_recall.h"
#include "framework/app_context.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"
#include <unordered_set>

namespace minisearchrec {

static float TagJaccard(const std::vector<std::string>& a, const std::unordered_set<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0f;
    int inter = 0;
    for (const auto& t : a) if (b.count(t)) ++inter;
    return (float)inter / (a.size() + b.size() - inter);
}

int HintTagRecall::ProcessDag(framework::DagProcessorContext* ctx) {
    if (!ctx->session) return 0;
    std::string doc_id = ctx->session->Get("doc_id");
    if (doc_id.empty()) { LOG_WARN("HintTagRecall: missing doc_id"); return 0; }

    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) { LOG_ERROR("HintTagRecall: DocStore not available"); return -1; }

    Document src_doc;
    if (!doc_store->GetDoc(doc_id, src_doc)) return 0;

    std::vector<std::string> src_tags(src_doc.tags().begin(), src_doc.tags().end());
    std::unordered_set<std::string> seen;
    seen.insert(doc_id);

    std::vector<DocCandidate> docs;
    for (const auto& id : doc_store->GetAllDocIds()) {
        if (seen.count(id)) continue;
        Document doc;
        if (!doc_store->GetDoc(id, doc)) continue;
        auto tags = std::vector<std::string>(doc.tags().begin(), doc.tags().end());
        float jaccard = TagJaccard(src_tags, std::unordered_set<std::string>(tags.begin(), tags.end()));
        if (jaccard > 0.1f && !doc.title().empty()) {
            DocCandidate cand;
            cand.doc_id = doc.title();
            cand.recall_source = "tag_match";
            cand.recall_score = 0.0f;
            cand.debug_scores["tag_relevance"] = jaccard;
            ctx->output->doc_scores.emplace_back(doc.title(), 0.0f);
            docs.push_back(std::move(cand));
            seen.insert(id);
        }
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("HintTagRecall: doc_id='{}', results={}", doc_id, ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(HintTagRecall);
