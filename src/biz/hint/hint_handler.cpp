#include "framework/class_register.h"
#include "biz/hint/hint_handler.h"
#include "biz/search/search_session.h"
#include "framework/config/config_manager.h"
#include "lib/storage/doc_cooccur_store.h"
#include "lib/rank/engine/rank_engine.h"
#include "utils/logger.h"
#include <json/json.h>

namespace minisearchrec {

int32_t HintBizHandler::ExtraInit() {
    const auto& global_cfg = ConfigManager::Instance().GetGlobalConfig();
    DocCooccurStore::Instance().Initialize(global_cfg.index.data_dir + "/docs.db");
    LOG_INFO("HintBizHandler::ExtraInit: complete");
    return 0;
}

int32_t HintBizHandler::MergeRecall(
    framework::Session* session,
    const std::vector<framework::RecallOutputPtr>& outputs) const {
    std::vector<DocCandidate> all_cands;
    for (const auto& output : outputs) {
        try {
            auto& cands = std::any_cast<const std::vector<DocCandidate>&>(output->items);
            all_cands.insert(all_cands.end(), cands.begin(), cands.end());
        } catch (...) {}
    }
    session->SetAny("hint_recall_docs", std::move(all_cands));
    return 0;
}

bool HintBizHandler::CanSearch(framework::Session* session) const {
    std::string doc_id = session->request.extra.count("doc_id") ? session->request.extra.at("doc_id") : "";
    if (doc_id.empty()) doc_id = session->Get("doc_id");
    if (doc_id.empty()) { LOG_DEBUG("HintBizHandler: missing doc_id"); return false; }
    session->Set("doc_id", doc_id);
    return true;
}

int32_t HintBizHandler::SetResponse(framework::Session* session) const {
    auto* items_ptr = session->GetAny<std::vector<rank::RankItem>>("hint_rank_vector");
    Json::Value root;
    root["ret"] = 0; root["err_msg"] = ""; root["search_id"] = session->search_id;
    Json::Value results(Json::arrayValue);

    if (items_ptr) {
        root["total"] = (int)items_ptr->size();
        for (const auto& item : *items_ptr) {
            Json::Value r;
            r["word"] = item.id; r["source"] = item.source; r["score"] = item.score;
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
REGISTER_MSR_HANDLER(HintBizHandler);
