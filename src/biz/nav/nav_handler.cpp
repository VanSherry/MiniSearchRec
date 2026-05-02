// ============================================================
#include "framework/class_register.h"
// MiniSearchRec - Nav Handler 实现（继承 framework::BaseHandler）
// 对标：通用搜索框架 NavHandler
//
// 各阶段：
//   CanSearch    → Nav 允许空 query（搜前引导不需要 query）
//   DoSearch     → 热词召回（QueryStats + DocStore + 预置词）
//   DoRank       → NavScoreProcessor（热度评分+去重+截断）
//   SetResponse  → 构造 JSON 响应
// ============================================================

#include "biz/nav/nav_handler.h"
#include "biz/nav/nav_factory.h"
#include "biz/nav/nav_rank_item.h"
#include "biz/nav/nav_context.h"
#include "biz/nav/processor/nav_score_processor.h"
#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_manager.h"
#include "utils/logger.h"

#include <json/json.h>
#include <chrono>
#include "ab/ab_test.h"
#include "framework/app_context.h"

namespace minisearchrec {

// ============================================================
// ExtraInit：业务初始化（框架注册 Handler 时自动调用）
// ============================================================
int32_t NavBizHandler::ExtraInit() {
    rank::BusinessRankConfig nav_config;
    nav_config.business_type = "nav";
    nav_config.factory_name = "NavFactory";
    nav_config.processors = {
        {"NavScoreProcessor", ""},
    };
    rank::RankManager::Instance().RegisterBusiness(nav_config);

    LOG_INFO("NavBizHandler::ExtraInit: complete");
    return 0;
}

// ============================================================
// CanSearch：Nav 允许空 query（搜前引导）
// ============================================================
bool NavBizHandler::CanSearch(framework::Session* session) const {
    // Nav 不需要 query，总是允许搜索
    // 但仍然检查 InterposeCheckQuery
    return InterposeCheckQuery(session);
}

// ============================================================
// DoSearch：热词召回
// ============================================================
int32_t NavBizHandler::DoSearch(framework::Session* session) const {
    // AB 实验染色
    auto ab_mgr = AppContext::Instance().GetABTestManager();
    if (ab_mgr && !session->uid.empty()) {
        const auto* exp = ab_mgr->AssignExperiment(session->uid);
        if (exp) {
            session->Set("ab_experiment_name", exp->name);
            std::string nav_count = ab_mgr->GetParam(session->uid, "nav_count", "");
            if (!nav_count.empty()) {
                session->Set("ab_nav_count", nav_count);
            }
        }
    }

    rank::RankArgs args;
    args.uid = session->uid;
    args.query = "";  // Nav 不需要 query
    args.business_type = "nav";
    args.page_size = session->request.page_size > 0 ? session->request.page_size : 6;

    auto* factory = rank::RankManager::Instance().GetFactory("nav");
    if (!factory) {
        LOG_ERROR("NavBizHandler::DoSearch: factory not found");
        return -1;
    }

    auto ranker = std::shared_ptr<rank::Rank>(factory->CreateRank());
    int ret = ranker->Init(args);
    if (ret != 0) {
        LOG_ERROR("NavBizHandler::DoSearch: rank init failed, ret={}", ret);
        return -2;
    }

    session->SetAny("nav_ranker", ranker);
    return 0;
}

// ============================================================
// DoRank：热度评分+去重+截断
// ============================================================
int32_t NavBizHandler::DoRank(framework::Session* session) const {
    auto* ranker_ptr = session->GetAny<std::shared_ptr<rank::Rank>>("nav_ranker");
    if (!ranker_ptr || !(*ranker_ptr)) {
        LOG_ERROR("NavBizHandler::DoRank: ranker not found");
        return -1;
    }

    int ret = (*ranker_ptr)->Process();
    if (ret != 0) {
        LOG_WARN("NavBizHandler::DoRank: process failed, ret={}", ret);
    }

    auto ctx = (*ranker_ptr)->GetContext();
    auto vec = ctx->GetVector();
    session->SetAny("nav_rank_vector", vec);

    return ret;
}

// ============================================================
// SetResponse：构造 JSON 响应
// ============================================================
int32_t NavBizHandler::SetResponse(framework::Session* session) const {
    auto* vec_ptr = session->GetAny<std::shared_ptr<rank::RankVector>>("nav_rank_vector");

    Json::Value root;
    root["ret"] = 0;
    root["err_msg"] = "";
    root["search_id"] = session->search_id;

    Json::Value results(Json::arrayValue);
    if (vec_ptr && *vec_ptr) {
        auto& vec = *(*vec_ptr);
        root["total"] = static_cast<int>(vec.Size());
        for (uint32_t i = 0; i < vec.Size(); ++i) {
            auto* item = static_cast<NavRankItem*>(vec.GetItem(i).get());
            if (!item) continue;
            Json::Value r;
            r["word"]      = item->Word();
            r["source"]    = item->source;
            r["hot_score"] = item->hot_score;
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
using namespace minisearchrec;
REGISTER_MSR_HANDLER(NavBizHandler);
