// ============================================================
// MiniSearchRec - BaseHandler 主流程实现
// 对标：通用搜索框架 BaseHandler::Search()
//
// 主流程 Pipeline：
//   InitSession → CanSearch(含 InterposeCheckQuery)
//   → PreSearch → DoSearch → DoRank → DoRerank
//   → DoInterpose(封禁/仅出/过滤) → SetResponse → ReportFinal
// ============================================================

#include "framework/handler/base_handler.h"
#include "framework/processor/processor_pipeline.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>
#include <regex>

namespace minisearchrec {
namespace framework {

// ============================================================
// Init
// ============================================================
int32_t BaseHandler::Init(const BusinessConfig& config) {
    config_ = config;
    int32_t ret = ExtraInit();
    if (ret != 0) {
        LOG_ERROR("BaseHandler::Init: ExtraInit failed for '{}', ret={}",
                  config_.handler_name, ret);
        return ret;
    }
    LOG_INFO("BaseHandler::Init: '{}' initialized", config_.handler_name);
    return 0;
}

int32_t BaseHandler::ExtraInit() { return 0; }

// ============================================================
// Search：主流程入口（模板方法）
// 对标通用搜索框架主流程
//
// 完整阶段对照：
//   通用搜索框架              MSR
//   ─────────────────────────     ─────────────
//   InitSession                   InitSession
//   CanSearch + InterposeCheckQuery  CanSearch (内含 InterposeCheckQuery)
//   PreSearch                     PreSearch
//   HandlerSearch                 DoSearch
//   BeforeHandlerRank + HandlerRank + AfterRank   DoRank
//   HandlerRerank                 DoRerank
//   DoInterpose + FilterInterposeOutput           DoInterpose
//   SetResponse                   SetResponse
//   ReportFinal                   ReportFinal (scope guard)
// ============================================================
int32_t BaseHandler::Search(Session* session) const {
    using Clock = std::chrono::steady_clock;
    int32_t ret = 0;

    // 0. InitSession
    ret = InitSession(session);
    if (ret != 0) {
        LOG_ERROR("{}: InitSession failed, ret={}", HandlerName(), ret);
        return ret;
    }

    // 确保 ReportFinal + BeforeDestruct 在退出时一定执行（对标 BOOST_SCOPE_EXIT）
    struct ScopeGuard {
        Session* s; const BaseHandler* h;
        ~ScopeGuard() { h->ReportFinal(s); s->BeforeDestruct(); }
    } guard{session, this};

    // 1. CanSearch（包含 InterposeCheckQuery：query 级封禁）
    if (!CanSearch(session)) {
        LOG_DEBUG("{}: CanSearch=false, returning empty", HandlerName());
        if (EmptyRespNoNeedInterpose(session)) {
            SetEmptyResponse(session);
            return 0;
        }
        // 不跳过干预 → 继续走 DoInterpose
        goto do_interpose;
    }

    // 2. PreSearch（预处理：Query 理解、实验染色等）
    {
        auto t0 = Clock::now();
        ret = PreSearch(session);
        session->metrics.presearch_cost_us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();

        if (ret < 0) {
            LOG_ERROR("{}: PreSearch failed, ret={}", HandlerName(), ret);
            return ret;
        }
        if (ret > 0) {
            LOG_DEBUG("{}: PreSearch rejected, ret={}", HandlerName(), ret);
            if (EmptyRespNoNeedInterpose(session)) {
                SetEmptyResponse(session);
                return 0;
            }
            goto do_interpose;
        }
    }

    // 3. DoSearch（召回）
    if (!config_.skip_search) {
        auto t0 = Clock::now();
        ret = DoSearch(session);
        session->metrics.search_cost_us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();

        if (ret < 0) {
            LOG_ERROR("{}: DoSearch failed, ret={}", HandlerName(), ret);
            return ret;
        }
        if (ret > 0) {
            LOG_DEBUG("{}: DoSearch rejected, ret={}", HandlerName(), ret);
            if (EmptyRespNoNeedInterpose(session)) {
                SetEmptyResponse(session);
                return 0;
            }
            goto do_interpose;
        }
    }

    // 4. DoRank（排序）
    if (!config_.skip_rank) {
        auto t0 = Clock::now();

        ret = BeforeRank(session);
        if (ret > 0) {
            if (EmptyRespNoNeedInterpose(session)) {
                SetEmptyResponse(session);
                return 0;
            }
            goto do_interpose;
        } else if (ret < 0) {
            LOG_ERROR("{}: BeforeRank failed, ret={}", HandlerName(), ret);
            return ret;
        }

        ret = DoRank(session);
        session->metrics.rank_cost_us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();

        if (ret != 0) {
            LOG_WARN("{}: DoRank failed (ret={}), trying fallback", HandlerName(), ret);
            ret = RankFallback(session);
            if (ret != 0) {
                LOG_ERROR("{}: RankFallback also failed", HandlerName());
                return ret;
            }
        }

        ret = AfterRank(session);
        if (ret != 0) {
            LOG_WARN("{}: AfterRank ret={}", HandlerName(), ret);
        }
    }

    // 5. DoRerank（重排序）
    // 对标通用搜索框架 Rerank 流程
    // 流程：SetRerankInput → CommonDoRerank → AfterRerank
    {
        auto t0 = Clock::now();
        ret = DoRerank(session);
        auto rerank_cost = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
        session->Set("rerank_cost_us", std::to_string(rerank_cost));

        if (ret != 0) {
            LOG_WARN("{}: DoRerank ret={}", HandlerName(), ret);
            // Rerank 失败不一定中断，取决于业务策略
        }
    }

do_interpose:
    // 6. DoInterpose（人工干预：封禁/仅出/过滤）
    // 对标通用搜索框架干预流程
    if (!config_.skip_interpose) {
        auto t0 = Clock::now();
        ret = DoInterpose(session);
        session->metrics.interpose_cost_us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();

        if (ret != 0) {
            LOG_WARN("{}: DoInterpose ret={}", HandlerName(), ret);
        }
    }

    // 7. SetResponse（组包）
    {
        auto t0 = Clock::now();
        ret = SetResponse(session);
        session->metrics.response_cost_us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();

        if (ret != 0) {
            LOG_ERROR("{}: SetResponse failed, ret={}", HandlerName(), ret);
            return ret;
        }
    }

    return 0;
}

// ============================================================
// CanSearch（含 InterposeCheckQuery）
// 对标通用搜索框架 CanSearch
// ============================================================
bool BaseHandler::CanSearch(Session* session) const {
    // query 级干预检查（封禁词 query 直接拒绝）
    if (!InterposeCheckQuery(session)) {
        LOG_DEBUG("{}: query '{}' blocked by InterposeCheckQuery", HandlerName(), session->query);
        return false;
    }

    // 空 query 检查
    if (session->query.empty()) {
        return false;
    }

    return true;
}

// ============================================================
// InterposeCheckQuery：query 级封禁检查
// 对标通用搜索框架查询干预检查
// 默认不封禁任何 query，业务可覆写添加黑名单逻辑
// ============================================================
bool BaseHandler::InterposeCheckQuery(Session* session) const {
    // 默认：不封禁任何 query
    // 业务覆写示例：
    //   auto& blacklist = BlacklistManager::Instance();
    //   if (blacklist.IsBlocked(session->query)) return false;
    return true;
}

// ============================================================
// DoInterpose：人工干预阶段
// 对标通用搜索框架干预流程
//
// 完整流程：
//   1. GetInterposeRules()  → 获取干预规则列表
//   2. ApplyInterpose()     → 执行封禁/仅出/提权/降权
//   3. FilterInterposeOutput() → 干预后过滤清理
// ============================================================
int32_t BaseHandler::DoInterpose(Session* session) const {
    // 1. 获取干预规则
    auto rules = GetInterposeRules(session);
    if (rules.empty()) {
        return 0;  // 无干预规则
    }

    LOG_DEBUG("{}: DoInterpose: {} rules loaded", HandlerName(), rules.size());

    // 2. 执行干预
    int32_t ret = ApplyInterpose(session, rules);
    if (ret != 0) {
        LOG_WARN("{}: ApplyInterpose ret={}", HandlerName(), ret);
    }

    // 3. 干预后过滤
    FilterInterposeOutput(session);

    return 0;
}

// ============================================================
// GetInterposeRules：获取干预规则
// 默认实现：从 Session 的 interpose_rules 中读取
// 业务可覆写：从配置文件/DB/远程服务加载
// ============================================================
std::vector<InterposeRule> BaseHandler::GetInterposeRules(Session* session) const {
    // 从 Session 的 any_store 中获取（业务在 InitSession 或 PreSearch 阶段注入）
    auto* rules = session->GetAny<std::vector<InterposeRule>>("interpose_rules");
    if (rules) {
        return *rules;
    }
    return {};
}

// ============================================================
// ApplyInterpose：执行干预逻辑
// 对标通用搜索框架干预处理
// ============================================================
int32_t BaseHandler::ApplyInterpose(Session* session,
                                    const std::vector<InterposeRule>& rules) const {
    // 获取当前结果列表（从 any_store 中读取，由 Rank 阶段写入）
    auto* result_words = session->GetAny<std::vector<std::string>>("result_words");
    if (!result_words) {
        return 0;  // 无结果可干预
    }

    std::vector<InterposeFilterRecord> filter_records;

    for (const auto& rule : rules) {
        switch (rule.action) {
        case InterposeRule::BLOCK: {
            // 封禁：从结果中删除匹配的词条
            auto it = std::remove_if(result_words->begin(), result_words->end(),
                [&](const std::string& word) {
                    bool match = (word == rule.pattern);
                    if (!match && !rule.pattern.empty()) {
                        try {
                            std::regex re(rule.pattern);
                            match = std::regex_search(word, re);
                        } catch (...) {}
                    }
                    if (match) {
                        filter_records.push_back({word, "BLOCKED", rule.reason});
                        LOG_DEBUG("{}: BLOCK '{}', reason: {}", HandlerName(), word, rule.reason);
                    }
                    return match;
                });
            result_words->erase(it, result_words->end());
            break;
        }
        case InterposeRule::FORCE_TOP: {
            // 仅出：将指定词条插入到指定位置
            int pos = (rule.position >= 0 && rule.position <= static_cast<int>(result_words->size()))
                      ? rule.position : 0;
            result_words->insert(result_words->begin() + pos, rule.pattern);
            LOG_DEBUG("{}: FORCE_TOP '{}' at pos {}", HandlerName(), rule.pattern, pos);
            break;
        }
        case InterposeRule::BOOST: {
            // 提权逻辑（业务在 Rank Processor 中处理更合适，这里记录标记）
            session->Set("interpose_boost_" + rule.pattern, std::to_string(rule.weight));
            break;
        }
        case InterposeRule::DEMOTE: {
            // 降权逻辑
            session->Set("interpose_demote_" + rule.pattern, std::to_string(rule.weight));
            break;
        }
        }
    }

    // 将过滤记录保存到 Session（供 ReportFinal 阶段上报）
    if (!filter_records.empty()) {
        session->SetAny("interpose_filter_records", std::move(filter_records));
    }

    return 0;
}

// ============================================================
// FilterInterposeOutput：干预后过滤
// 对标通用搜索框架干预结果过滤
// ============================================================
void BaseHandler::FilterInterposeOutput(Session* session) const {
    // 默认：无额外过滤
    // 业务覆写可在此做最终的结果清洗（如去重、长度截断等）
}

// ============================================================
// 各阶段默认实现
// ============================================================

int32_t BaseHandler::InitSession(Session* session) const {
    return 0;
}

int32_t BaseHandler::PreSearch(Session* session) const {
    int32_t ret = CommonPreSearch(session);
    if (ret != 0) return ret;
    return ExtraPreSearch(session);
}

int32_t BaseHandler::CommonPreSearch(Session* session) const {
    // AB 实验染色（对标通用搜索框架 AB 实验染色）
    // 由框架统一处理，业务不需要单独接入
    if (!session->uid.empty()) {
        session->Set("ab_experiment", "");  // 标记已经过实验染色
        // 实际 AB 分流在 search_factory.cpp 中通过 AppContext::GetABTestManager 完成
        // 框架层只做标记，确保链路可追踪
    }
    return 0;
}
int32_t BaseHandler::ExtraPreSearch(Session* session) const { return 0; }

int32_t BaseHandler::DoSearch(Session* session) const {
    int32_t ret = CommonDoSearch(session);
    if (ret != 0) return ret;
    return ExtraDoSearch(session);
}

int32_t BaseHandler::CommonDoSearch(Session* session) const {
    // 默认：通过 PipelineManager 调度 recall_pipeline
    auto* pipeline_cfg = PipelineManager::Instance().GetConfig(config_.business_type);
    if (pipeline_cfg && pipeline_cfg->recall_pipeline.Size() > 0) {
        return pipeline_cfg->recall_pipeline.Execute(session);
    }
    return 0;
}
int32_t BaseHandler::ExtraDoSearch(Session* session) const { return 0; }

int32_t BaseHandler::DoRank(Session* session) const {
    int32_t ret = CommonDoRank(session);
    if (ret != 0) return ret;
    return ExtraDoRank(session);
}

int32_t BaseHandler::BeforeRank(Session* session) const { return 0; }
int32_t BaseHandler::CommonDoRank(Session* session) const {
    // 默认：通过 PipelineManager 调度 rank_pipeline（粗排）
    auto* pipeline_cfg = PipelineManager::Instance().GetConfig(config_.business_type);
    if (pipeline_cfg && pipeline_cfg->rank_pipeline.Size() > 0) {
        return pipeline_cfg->rank_pipeline.Execute(session);
    }
    return 0;
}
int32_t BaseHandler::ExtraDoRank(Session* session) const { return 0; }
int32_t BaseHandler::AfterRank(Session* session) const { return 0; }

int32_t BaseHandler::DoRerank(Session* session) const {
    // 对标通用搜索框架 Rerank 完整流程
    // 默认：如果未配置 rerank，直接跳过

    // 1. SetRerankInput
    int32_t ret = SetRerankInput(session);
    if (ret != 0) {
        LOG_WARN("{}: SetRerankInput failed, ret={}", HandlerName(), ret);
        return ret;
    }

    // 2. CommonDoRerank（核心重排序）
    ret = CommonDoRerank(session);
    if (ret != 0) {
        LOG_WARN("{}: CommonDoRerank failed, ret={}", HandlerName(), ret);
        // 兜底策略：是否用 Rank 结果
        if (IsRerankFailUseRankOutput(session)) {
            LOG_INFO("{}: Rerank failed, fallback to Rank output", HandlerName());
            return 0;
        }
        return ret;
    }

    // 3. AfterRerank（后处理：结果替换、特征保留等）
    ret = AfterRerank(session);
    if (ret != 0) {
        LOG_WARN("{}: AfterRerank ret={}", HandlerName(), ret);
    }

    return 0;
}

int32_t BaseHandler::SetRerankInput(Session* session) const {
    // 默认：无需构建 Rerank 输入（业务覆写时从 Rank output 填充）
    return 0;
}

int32_t BaseHandler::CommonDoRerank(Session* session) const {
    // 默认：通过 PipelineManager 调度 rerank_pipeline（精排）
    auto* pipeline_cfg = PipelineManager::Instance().GetConfig(config_.business_type);
    if (pipeline_cfg && pipeline_cfg->rerank_pipeline.Size() > 0) {
        return pipeline_cfg->rerank_pipeline.Execute(session);
    }
    return 0;
}

int32_t BaseHandler::AfterRerank(Session* session) const {
    // 默认：无后处理
    return 0;
}

int32_t BaseHandler::SetResponse(Session* session) const {
    int32_t ret = CommonSetResponse(session);
    if (ret != 0) return ret;
    return ExtraSetResponse(session);
}

int32_t BaseHandler::CommonSetResponse(Session* session) const { return 0; }
int32_t BaseHandler::ExtraSetResponse(Session* session) const { return 0; }

int32_t BaseHandler::ReportFinal(Session* session) const {
    auto now = std::chrono::steady_clock::now();
    session->metrics.total_cost_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - session->begin_time).count();

    // 上报干预过滤记录
    auto* records = session->GetAny<std::vector<InterposeFilterRecord>>("interpose_filter_records");
    int interpose_count = records ? static_cast<int>(records->size()) : 0;

    LOG_INFO("{}: Search complete, query='{}', uid={}, total_cost={}us, "
             "presearch={}us, search={}us, rank={}us, interpose={}us(filtered:{}), resp={}us",
             HandlerName(), session->query, session->uid,
             session->metrics.total_cost_us,
             session->metrics.presearch_cost_us,
             session->metrics.search_cost_us,
             session->metrics.rank_cost_us,
             session->metrics.interpose_cost_us, interpose_count,
             session->metrics.response_cost_us);
    return 0;
}

void BaseHandler::SetEmptyResponse(Session* session) const {
    session->response.ret = 0;
    session->response.total = 0;
    session->response.items_json = "[]";
}

int32_t BaseHandler::RankFallback(Session* session) const { return 0; }

} // namespace framework
} // namespace minisearchrec
