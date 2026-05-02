// ============================================================
// MiniSearchRec - 自动索引重建任务实现
// ============================================================

#include "scheduler/task/index_rebuild_task.h"
#include "framework/app_context.h"
#include "framework/config/config_manager.h"
#include "lib/index/inverted_index.h"
#include "lib/index/vector_index.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"

namespace minisearchrec {
namespace scheduler {

void IndexRebuildTask::CheckAndRun() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - last_run_).count();

    if (elapsed < cfg_.interval_hours) return;

    LOG_INFO("IndexRebuild: interval reached ({}h), starting rebuild", elapsed);

    if (RebuildAtomically()) {
        LOG_INFO("IndexRebuild: rebuild and swap complete");
        last_run_ = std::chrono::steady_clock::now();
    } else {
        LOG_ERROR("IndexRebuild: rebuild failed, keeping old index");
    }
}

bool IndexRebuildTask::RebuildAtomically() {
    auto& ctx = AppContext::Instance();
    auto doc_store = ctx.GetDocStore();
    if (!doc_store) {
        LOG_ERROR("IndexRebuild: DocStore not available");
        return false;
    }

    auto all_ids = doc_store->GetAllDocIds();
    if (all_ids.empty()) {
        LOG_WARN("IndexRebuild: no documents, skipping");
        return false;
    }

    LOG_INFO("IndexRebuild: rebuilding from {} documents", all_ids.size());

    auto new_inverted = std::make_shared<InvertedIndex>();
    auto emb_provider = ctx.GetEmbeddingProvider();
    VectorIndexConfig vec_cfg;
    vec_cfg.dim = emb_provider->GetDim();
    auto new_vector = std::make_shared<VectorIndex>(vec_cfg);

    for (const auto& doc_id : all_ids) {
        Document doc;
        if (!doc_store->GetDoc(doc_id, doc)) continue;

        std::vector<std::string> tags;
        for (int i = 0; i < doc.tags_size(); ++i) tags.push_back(doc.tags(i));

        new_inverted->AddDocument(doc.doc_id(), doc.title(), doc.content(),
                                  doc.category(), tags, doc.content_length());

        if (emb_provider) {
            auto emb = emb_provider->Encode(doc.title() + " " + doc.content());
            new_vector->AddVector(doc.doc_id(), emb);
        }
    }

    LOG_INFO("IndexRebuild: new index built, docs={}", new_inverted->GetDocCount());

    ctx.SwapIndexes(new_inverted, new_vector);

    const auto& index_dir = ConfigManager::Instance().GetGlobalConfig().index.index_dir;
    new_inverted->Save(index_dir + "/inverted.idx");
    new_vector->Save(index_dir + "/vector.faiss");

    return true;
}

} // namespace scheduler
} // namespace minisearchrec
