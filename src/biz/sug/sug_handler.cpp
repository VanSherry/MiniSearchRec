// ============================================================
#include "framework/class_register.h"
// MiniSearchRec - Sug Handler 实现（继承 framework::BaseHandler）
// 对标：通用搜索框架 SuggesterHandler
//
// 各阶段对标：
//   CanSearch    → 空 query 检查
//   DoSearch     → Trie 召回 + RankManager 创建 Rank 实例
//   DoRank       → Processor 链执行（SugRelevance → SugPostRank）
//   DoInterpose  → 黑名单词过滤 + 低质量词过滤
//   SetResponse  → 构造 JSON 响应 + 高亮 + query_stats 写入
// ============================================================

#include "biz/sug/sug_handler.h"
#include "framework/config/config_manager.h"
#include "biz/sug/sug_trie.h"
#include "biz/sug/sug_factory.h"
#include "biz/sug/sug_rank_item.h"
#include "biz/sug/sug_context.h"
#include "biz/sug/processor/sug_relevance_processor.h"
#include "biz/sug/processor/sug_post_rank_processor.h"
#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_manager.h"
#include "framework/app_context.h"
#include "lib/storage/query_stats_store.h"
#include "lib/index/doc_store.h"
#include "utils/logger.h"

#include <json/json.h>
#include <chrono>
#include <ctime>
#include <algorithm>
#include "ab/ab_test.h"
#include "framework/app_context.h"

namespace minisearchrec {

// ============================================================
// ExtraInit：业务初始化（框架注册 Handler 时自动调用，不需要 main.cpp 手动调）
// ============================================================
int32_t SugBizHandler::ExtraInit() {
    const auto& global_cfg = ConfigManager::Instance().GetGlobalConfig();
    const std::string data_dir = global_cfg.index.data_dir;

    // 初始化 QueryStatsStore（幂等，多次调用无副作用）
    QueryStatsStore::Instance().Initialize(data_dir + "/docs.db");

    // 注册 SugFactory 和 Processor 链到 RankManager
    rank::BusinessRankConfig sug_config;
    sug_config.business_type = "sug";
    sug_config.factory_name = "SugFactory";
    sug_config.processors = {
        {"SugRelevanceProcessor", ""},
        {"SugPostRankProcessor", ""},
    };
    rank::RankManager::Instance().RegisterBusiness(sug_config);

    // 从 DocStore 导入词条到 Trie
    RebuildTrie();

    LOG_INFO("SugBizHandler::ExtraInit: complete");
    return 0;
}

// ============================================================
// RebuildTrie：从 DocStore + QueryStats 重建 Trie 词库
// ============================================================
void SugBizHandler::RebuildTrie() {
    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) {
        LOG_WARN("SugBizHandler::RebuildTrie: DocStore not available");
        return;
    }

    std::vector<TrieEntry> entries;
    int64_t now = std::time(nullptr);

    // 来源1：文档标题
    auto doc_ids = doc_store->GetAllDocIds();
    for (const auto& doc_id : doc_ids) {
        Document doc;
        if (!doc_store->GetDoc(doc_id, doc)) continue;

        if (!doc.title().empty()) {
            TrieEntry e;
            e.word = doc.title();
            e.source = "title";
            e.freq = std::max((int64_t)1, (int64_t)doc.click_count());
            e.last_time = doc.publish_time() > 0 ? doc.publish_time() : now;
            e.source_weight = 1.0f;
            entries.push_back(std::move(e));
        }

        // 来源2：文档标签
        for (const auto& tag : doc.tags()) {
            if (tag.empty() || tag.size() < 2) continue;
            TrieEntry e;
            e.word = tag;
            e.source = "tag";
            e.freq = std::max((int64_t)1, (int64_t)(doc.click_count() / 2));
            e.last_time = doc.publish_time() > 0 ? doc.publish_time() : now;
            e.source_weight = 0.8f;
            entries.push_back(std::move(e));
        }
    }

    // 来源3：用户历史搜索词（从 QueryStatsStore）
    auto& qs = QueryStatsStore::Instance();
    auto top_queries = qs.GetTopN(200);
    for (const auto& q : top_queries) {
        TrieEntry e;
        e.word = q.query;
        e.source = "user_query";
        e.freq = q.freq;
        e.last_time = q.last_time;
        e.source_weight = 1.2f;
        entries.push_back(std::move(e));
    }

    SugTrie::Instance().Build(std::move(entries));
    SugTrie::Instance().ClearRebuildMark();
    LOG_INFO("SugBizHandler::RebuildTrie: trie rebuilt, size={}", SugTrie::Instance().Size());
}


// ============================================================
// CanSearch：前置检查
// 对标 SuggesterHandler::CanSearch
// ============================================================
bool SugBizHandler::CanSearch(framework::Session* session) const {
    // Sug 需要 query（前缀），空 query 不允许
    if (session->query.empty()) {
        LOG_DEBUG("SugBizHandler: empty query, rejected");
        return false;
    }
    // 调用框架默认的 InterposeCheckQuery
    if (!InterposeCheckQuery(session)) {
        return false;
    }
    return true;
}

// ============================================================
// DoSearch：召回阶段（Trie 前缀召回 + 构建 Rank 实例）
// 对标 SuggesterHandler::CommonHandlerSearch (Trie 召回)
// ============================================================
int32_t SugBizHandler::DoSearch(framework::Session* session) const {
    // AB 实验染色
    auto ab_mgr = AppContext::Instance().GetABTestManager();
    if (ab_mgr && !session->uid.empty()) {
        const auto* exp = ab_mgr->AssignExperiment(session->uid);
        if (exp) {
            session->Set("ab_experiment_name", exp->name);
            // 读取实验参数（如 sug 候选数、相关性阈值等）
            std::string sug_top_n = ab_mgr->GetParam(session->uid, "sug_top_n", "");
            if (!sug_top_n.empty()) {
                session->Set("ab_sug_top_n", sug_top_n);
            }
        }
    }

    // 构建 RankArgs
    rank::RankArgs args;
    args.uid = session->uid;
    args.query = session->query;
    args.business_type = "sug";
    args.page_size = session->request.page_size;

    // 2. 创建 Rank 实例（内含 PrepareInput 即 Trie 召回）
    auto* factory = rank::RankManager::Instance().GetFactory("sug");
    if (!factory) {
        LOG_ERROR("SugBizHandler::DoSearch: factory not found");
        return -1;
    }

    auto ranker = std::shared_ptr<rank::Rank>(factory->CreateRank());
    int ret = ranker->Init(args);
    if (ret != 0) {
        LOG_ERROR("SugBizHandler::DoSearch: rank init failed, ret={}", ret);
        return -2;
    }

    // 将 ranker 存入 session 供后续阶段使用
    session->SetAny("sug_ranker", ranker);
    return 0;
}

// ============================================================
// DoRank：排序阶段（执行 Processor 链）
// 对标 SuggesterHandler::HandleRankFull → RankMgr → Processor链
// ============================================================
int32_t SugBizHandler::DoRank(framework::Session* session) const {
    auto* ranker_ptr = session->GetAny<std::shared_ptr<rank::Rank>>("sug_ranker");
    if (!ranker_ptr || !(*ranker_ptr)) {
        LOG_ERROR("SugBizHandler::DoRank: ranker not found in session");
        return -1;
    }

    // Process() 内部执行：PrepareInput(Trie召回) → SugRelevanceProcessor → SugPostRankProcessor
    int ret = (*ranker_ptr)->Process();
    if (ret != 0) {
        LOG_WARN("SugBizHandler::DoRank: process failed, ret={}", ret);
    }

    // 将结果存入 session
    auto ctx = (*ranker_ptr)->GetContext();
    auto vec = ctx->GetVector();
    session->SetAny("sug_rank_vector", vec);

    return ret;
}

// ============================================================
// DoInterpose：干预阶段
// 对标 SuggesterHandler::DoInterpose
// Sug 的干预：低质量词过滤、敏感词过滤、去重
// ============================================================
int32_t SugBizHandler::DoInterpose(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<std::shared_ptr<rank::RankVector>>("sug_rank_vector");
    if (!vec_ptr || !(*vec_ptr)) {
        return 0;  // 无结果可干预
    }

    auto& vec = *(*vec_ptr);
    std::vector<framework::InterposeFilterRecord> filter_records;

    // 1. 获取干预规则
    auto rules = GetInterposeRules(session);

    // 2. 遍历结果，执行封禁过滤
    for (const auto& rule : rules) {
        if (rule.action == framework::InterposeRule::BLOCK) {
            for (uint32_t i = 0; i < vec.Size(); ) {
                auto* item = static_cast<SugRankItem*>(vec.GetItem(i).get());
                if (item && item->Word() == rule.pattern) {
                    filter_records.push_back({item->Word(), "BLOCKED", rule.reason});
                    LOG_DEBUG("SugBizHandler: BLOCK sug word '{}'", item->Word());
                    vec.SetItemFilter(i, "interpose_block");
                } else {
                    ++i;
                }
            }
        }
    }

    // 3. 低质量词过滤（分数过低的词条移到过滤列表）
    for (uint32_t i = 0; i < vec.Size(); ) {
        auto* item = static_cast<SugRankItem*>(vec.GetItem(i).get());
        if (item && item->final_score < 0.01f) {
            filter_records.push_back({item->Word(), "LOW_QUALITY", "score<0.01"});
            vec.SetItemFilter(i, "low_quality");
        } else {
            ++i;
        }
    }

    if (!filter_records.empty()) {
        session->SetAny("interpose_filter_records",
                        std::vector<framework::InterposeFilterRecord>(std::move(filter_records)));
        LOG_INFO("SugBizHandler::DoInterpose: filtered {} items", filter_records.size());
    }

    return 0;
}

// ============================================================
// SetResponse：组包阶段（构造 JSON 响应）
// 对标 SuggesterHandler::ExtraSetResponse
// ============================================================
int32_t SugBizHandler::SetResponse(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<std::shared_ptr<rank::RankVector>>("sug_rank_vector");

    Json::Value root;
    root["ret"] = 0;
    root["err_msg"] = "";
    root["search_id"] = session->search_id;

    Json::Value results(Json::arrayValue);

    if (vec_ptr && *vec_ptr) {
        auto& vec = *(*vec_ptr);
        root["total"] = static_cast<int>(vec.Size());

        for (uint32_t i = 0; i < vec.Size(); ++i) {
            auto* item = static_cast<SugRankItem*>(vec.GetItem(i).get());
            if (!item) continue;

            Json::Value r;
            r["word"]          = item->Word();
            r["source"]        = item->source;
            r["score"]         = item->final_score;
            r["highlight_len"] = static_cast<int>(session->query.size());
            results.append(r);
        }
    } else {
        root["total"] = 0;
    }

    root["results"] = results;

    // 写入 query_stats（搜索行为积累）
    if (!session->query.empty()) {
        QueryStatsStore::Instance().IncrementQuery(session->query, "user_query");
    }

    // 序列化到 session->response
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    session->response.ret = 0;
    session->response.total = results.size();
    session->response.items_json = Json::writeString(writer, root);
    session->response.search_id = session->search_id;

    return 0;
}

} // namespace minisearchrec

// 注册到框架反射表（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_HANDLER(SugBizHandler);
