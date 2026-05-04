#include "framework/class_register.h"
#include "biz/sug/sug_handler.h"
#include "biz/sug/sug_trie.h"
#include "biz/search/search_session.h"
#include "framework/config/config_manager.h"
#include "framework/app_context.h"
#include "lib/storage/query_stats_store.h"
#include "lib/index/doc_store.h"
#include "lib/rank/base/rank_vector.h"
#include "utils/logger.h"
#include <json/json.h>
#include <ctime>
#include <cmath>

namespace minisearchrec {

int32_t SugBizHandler::ExtraInit() {
    const auto& global_cfg = ConfigManager::Instance().GetGlobalConfig();
    QueryStatsStore::Instance().Initialize(global_cfg.index.data_dir + "/docs.db");
    RebuildTrie();
    LOG_INFO("SugBizHandler::ExtraInit: complete");
    return 0;
}

void SugBizHandler::RebuildTrie() {
    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) { LOG_WARN("SugBizHandler::RebuildTrie: DocStore not available"); return; }

    std::vector<TrieEntry> entries;
    int64_t now = std::time(nullptr);

    for (const auto& did : doc_store->GetAllDocIds()) {
        Document doc; if (!doc_store->GetDoc(did, doc)) continue;
        int64_t pt = doc.publish_time() > 0 ? doc.publish_time() : now;
        if (!doc.title().empty()) {
            TrieEntry e; e.word = doc.title(); e.source = "title";
            e.freq = std::max((int64_t)1, doc.click_count()); e.last_time = pt; e.source_weight = 1.0f;
            entries.push_back(std::move(e));
        }
        for (const auto& tag : doc.tags()) {
            if (tag.size() >= 2) {
                TrieEntry e; e.word = tag; e.source = "tag";
                e.freq = std::max((int64_t)1, doc.click_count() / 2);
                e.last_time = pt; e.source_weight = 0.8f;
                entries.push_back(std::move(e));
            }
        }
    }

    for (const auto& q : QueryStatsStore::Instance().GetTopN(200)) {
        TrieEntry e; e.word = q.query; e.source = "user_query";
        e.freq = q.freq; e.last_time = q.last_time; e.source_weight = 1.2f;
        entries.push_back(std::move(e));
    }

    SugTrie::Instance().Build(std::move(entries));
    SugTrie::Instance().ClearRebuildMark();
    LOG_INFO("SugBizHandler::RebuildTrie: trie rebuilt, size={}", SugTrie::Instance().Size());
}

int32_t SugBizHandler::MergeRecall(
    framework::Session* session,
    const std::vector<framework::RecallOutputPtr>& outputs) const {
    // 聚合所有 DAG 召回结果存入 any_store，供 SugRank::PrepareInput 使用
    std::vector<DocCandidate> all_cands;
    for (const auto& output : outputs) {
        try {
            auto& cands = std::any_cast<const std::vector<DocCandidate>&>(output->items);
            all_cands.insert(all_cands.end(), cands.begin(), cands.end());
        } catch (...) {}
    }
    session->SetAny("sug_recall_docs", std::move(all_cands));
    return 0;
}

int32_t SugBizHandler::AfterRank(framework::Session* session) const {
    // GenerateRankOutput 已排序+截断，无需额外处理
    return 0;
}

bool SugBizHandler::CanSearch(framework::Session* session) const {
    if (session->query.empty()) { LOG_DEBUG("SugBizHandler: empty query, rejected"); return false; }
    return InterposeCheckQuery(session);
}

int32_t SugBizHandler::SetResponse(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<rank::RankVectorPtr>("sug_rank_vector");
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
            r["score"] = item->Score(); r["highlight_len"] = (int)session->query.size();
            results.append(r);
        }
    } else root["total"] = 0;
    root["results"] = results;

    if (!session->query.empty()) QueryStatsStore::Instance().IncrementQuery(session->query, "user_query");
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    session->response.ret = 0; session->response.total = results.size();
    session->response.items_json = Json::writeString(writer, root);
    session->response.search_id = session->search_id;
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_HANDLER(SugBizHandler);
