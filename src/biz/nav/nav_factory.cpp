#include "biz/nav/nav_factory.h"
#include "biz/search/search_session.h"
#include "lib/rank/base/rank_manager.h"
#include "utils/logger.h"

namespace minisearchrec {

int NavRank::PrepareInput() {
    auto* session = ctx_->GetFrameworkSession();
    if (!session) return -1;

    auto* docs_ptr = session->GetAny<std::vector<DocCandidate>>("nav_recall_docs");
    if (!docs_ptr || docs_ptr->empty()) {
        LOG_DEBUG("NavRank::PrepareInput: no recall docs");
        return 0;
    }

    auto vec = ctx_->GetVector();
    vec->Clear();

    for (auto& cand : *docs_ptr) {
        auto item = std::make_shared<rank::BaseRankItem>();
        item->SetWord(cand.doc_id);
        item->SetDesc(cand.recall_source);
        for (auto& [k, v] : cand.debug_scores) {
            item->SetFeature(k, v);
        }
        vec->PushBack(item);
    }

    LOG_INFO("NavRank::PrepareInput: items={}", vec->Size());
    return 0;
}

int NavRank::GenerateRankOutput() {
    auto vec = ctx_->GetVector();
    if (!vec || vec->Size() == 0) return 0;

    vec->SortByScore();
    int max_results = 6;
    vec->Truncate(max_results);

    auto* session = ctx_->GetFrameworkSession();
    if (session) {
        auto rank_vec = std::make_shared<rank::RankVector>(*vec);
        session->SetAny("nav_rank_vector", rank_vec);
    }

    LOG_INFO("NavRank::GenerateRankOutput: sorted+truncated to {}", vec->Size());
    return 0;
}

REGISTER_RANK_FACTORY(NavFactory);

} // namespace minisearchrec
