// ============================================================
#include "framework/class_register.h"
// MiniSearchRec - Hint Handler 实现（继承 framework::BaseHandler）
// 对标：通用搜索框架 ClickHintHandler
//
// 各阶段：
//   CanSearch    → doc_id 必填检查
//   DoSearch     → 四路召回（RankManager → HintFactory → PrepareInput）
//   DoRank       → Processor 链（HintScore → HintPostRank）
//   DoInterpose  → 结果去重 + 低质量过滤
//   SetResponse  → 构造 JSON 响应
// ============================================================

#include "biz/hint/hint_handler.h"
#include "framework/config/config_manager.h"
#include "biz/hint/hint_factory.h"
#include "biz/hint/hint_rank_item.h"
#include "biz/hint/hint_context.h"
#include "biz/hint/processor/hint_rank_processor.h"
#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_manager.h"
#include "lib/storage/doc_cooccur_store.h"
#include "utils/logger.h"

#include <json/json.h>
#include <chrono>
#include <unordered_set>
#include "ab/ab_test.h"

namespace minisearchrec {

// ============================================================
// ExtraInit：业务初始化（框架注册 Handler 时自动调用）
// ============================================================
int32_t HintBizHandler::ExtraInit() {
    const auto& global_cfg = ConfigManager::Instance().GetGlobalConfig();
    const std::string data_dir = global_cfg.index.data_dir;

    // 初始化共现存储
    DocCooccurStore::Instance().Initialize(data_dir + "/docs.db");

    // 注册 Factory + Processor
    rank::BusinessRankConfig hint_config;
    hint_config.business_type = "hint";
    hint_config.factory_name = "HintFactory";
    hint_config.processors = {
        {"HintScoreProcessor", ""},
        {"HintPostRankProcessor", ""},
    };
    rank::RankManager::Instance().RegisterBusiness(hint_config);

    LOG_INFO("HintBizHandler::ExtraInit: complete");
    return 0;
}

// ============================================================
// CanSearch：Hint 需要 doc_id（点后推的源文档）
// ============================================================
bool HintBizHandler::CanSearch(framework::Session* session) const {
    // Hint 的 query 是可选的，但 doc_id 是必填的
    std::string doc_id = session->request.extra.count("doc_id")
                         ? session->request.extra.at("doc_id") : "";
    if (doc_id.empty()) {
        // 尝试从 query 参数中获取
        doc_id = session->Get("doc_id");
    }
    if (doc_id.empty()) {
        LOG_DEBUG("HintBizHandler: missing doc_id");
        return false;
    }
    session->Set("doc_id", doc_id);
    return true;
}

// ============================================================
// DoSearch：四路召回
// ============================================================
int32_t HintBizHandler::DoSearch(framework::Session* session) const {
    // AB 实验染色
    auto ab_mgr = AppContext::Instance().GetABTestManager();
    if (ab_mgr && !session->uid.empty()) {
        const auto* exp = ab_mgr->AssignExperiment(session->uid);
        if (exp) {
            session->Set("ab_experiment_name", exp->name);
            std::string hint_top_n = ab_mgr->GetParam(session->uid, "hint_top_n", "");
            if (!hint_top_n.empty()) {
                session->Set("ab_hint_top_n", hint_top_n);
            }
        }
    }

    rank::RankArgs args;
    args.uid = session->uid;
    args.query = session->query;
    args.doc_id = session->Get("doc_id");
    args.business_type = "hint";
    args.page_size = session->request.page_size;

    auto* factory = rank::RankManager::Instance().GetFactory("hint");
    if (!factory) {
        LOG_ERROR("HintBizHandler::DoSearch: factory not found");
        return -1;
    }

    auto ranker = std::unique_ptr<rank::Rank>(factory->CreateRank());
    int ret = ranker->Init(args);
    if (ret != 0) {
        LOG_ERROR("HintBizHandler::DoSearch: rank init failed, ret={}", ret);
        return -2;
    }

    session->SetAny("hint_ranker", std::move(ranker));
    return 0;
}

// ============================================================
// DoRank：执行 Processor 链
// ============================================================
int32_t HintBizHandler::DoRank(framework::Session* session) const {
    auto* ranker_ptr = session->GetAny<std::unique_ptr<rank::Rank>>("hint_ranker");
    if (!ranker_ptr || !(*ranker_ptr)) {
        LOG_ERROR("HintBizHandler::DoRank: ranker not found");
        return -1;
    }

    int ret = (*ranker_ptr)->Process();
    if (ret != 0) {
        LOG_WARN("HintBizHandler::DoRank: process failed, ret={}", ret);
    }

    auto ctx = (*ranker_ptr)->GetContext();
    auto vec = ctx->GetVector();
    session->SetAny("hint_rank_vector", vec);

    return ret;
}

// ============================================================
// DoInterpose：Hint 干预（去重 + 过滤与 query 完全相同的结果）
// 对标 ClickHintHandler::DoInterpose
// ============================================================
int32_t HintBizHandler::DoInterpose(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<std::shared_ptr<rank::RankVector>>("hint_rank_vector");
    if (!vec_ptr || !(*vec_ptr)) return 0;

    auto& vec = *(*vec_ptr);
    std::vector<framework::InterposeFilterRecord> records;

    // 过滤与 query 完全相同的结果（用户刚搜的词不应出现在 hint 中）
    for (uint32_t i = 0; i < vec.Size(); ) {
        auto* item = static_cast<HintRankItem*>(vec.GetItem(i).get());
        if (item && item->Word() == session->query) {
            records.push_back({item->Word(), "SAME_AS_QUERY", ""});
            vec.SetItemFilter(i, "same_as_query");
        } else {
            ++i;
        }
    }

    // 获取并应用外部干预规则
    auto rules = GetInterposeRules(session);
    for (const auto& rule : rules) {
        if (rule.action == framework::InterposeRule::BLOCK) {
            for (uint32_t i = 0; i < vec.Size(); ) {
                auto* item = static_cast<HintRankItem*>(vec.GetItem(i).get());
                if (item && item->Word() == rule.pattern) {
                    records.push_back({item->Word(), "BLOCKED", rule.reason});
                    vec.SetItemFilter(i, "interpose_block");
                } else {
                    ++i;
                }
            }
        }
    }

    if (!records.empty()) {
        session->SetAny("interpose_filter_records",
                        std::vector<framework::InterposeFilterRecord>(std::move(records)));
    }

    return 0;
}

// ============================================================
// SetResponse：构造 JSON 响应
// ============================================================
int32_t HintBizHandler::SetResponse(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<std::shared_ptr<rank::RankVector>>("hint_rank_vector");

    Json::Value root;
    root["ret"] = 0;
    root["err_msg"] = "";
    root["search_id"] = session->search_id;

    Json::Value results(Json::arrayValue);
    if (vec_ptr && *vec_ptr) {
        auto& vec = *(*vec_ptr);
        root["total"] = static_cast<int>(vec.Size());
        for (uint32_t i = 0; i < vec.Size(); ++i) {
            auto* item = static_cast<HintRankItem*>(vec.GetItem(i).get());
            if (!item) continue;
            Json::Value r;
            r["word"]   = item->Word();
            r["source"] = item->source;
            r["score"]  = item->final_score;
            results.append(r);
        }
    } else {
        root["total"] = 0;
    }
    root["results"] = results;

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
REGISTER_MSR_HANDLER(HintBizHandler);
