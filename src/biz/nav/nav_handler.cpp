#include "framework/class_register.h"
#include "biz/nav/nav_handler.h"
#include "biz/search/search_session.h"
#include "lib/rank/base/rank_vector.h"
#include "utils/logger.h"
#include <json/json.h>

namespace minisearchrec {

int32_t NavBizHandler::ExtraInit() {
    LOG_INFO("NavBizHandler::ExtraInit: complete");
    return 0;
}

int32_t NavBizHandler::MergeRecall(
    framework::Session* session,
    const std::vector<framework::RecallOutputPtr>& outputs) const {
    std::vector<DocCandidate> all_cands;
    for (const auto& output : outputs) {
        try {
            auto& cands = std::any_cast<const std::vector<DocCandidate>&>(output->items);
            all_cands.insert(all_cands.end(), cands.begin(), cands.end());
        } catch (...) {}
    }
    session->SetAny("nav_recall_docs", std::move(all_cands));
    return 0;
}

int32_t NavBizHandler::AfterRank(framework::Session* session) const {
    return 0;
}

bool NavBizHandler::CanSearch(framework::Session* session) const {
    return InterposeCheckQuery(session);
}

int32_t NavBizHandler::SetResponse(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<rank::RankVectorPtr>("nav_rank_vector");
    Json::Value root;
    root["ret"] = 0; root["err_msg"] = ""; root["search_id"] = session->search_id;
    Json::Value results(Json::arrayValue);

    if (vec_ptr && *vec_ptr) {
        auto& vec = **vec_ptr;
        root["total"] = (int)vec.Size();
        for (uint32_t i = 0; i < vec.Size(); ++i) {
            auto* item = vec.GetItem(i).get();
            if (!item) continue;
            Json::Value r;
            r["word"] = item->Word(); r["source"] = item->Desc();
            r["hot_score"] = item->Score();
            results.append(r);
        }
    } else root["total"] = 0;
    root["results"] = results;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    session->response.ret = 0; session->response.total = results.size();
    session->response.items_json = Json::writeString(writer, root);
    session->response.search_id = session->search_id;
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_HANDLER(NavBizHandler);
