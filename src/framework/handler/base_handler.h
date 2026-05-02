// ============================================================
// MiniSearchRec - BaseHandler (主流程框架基类)
// 对标：通用搜索框架 BaseHandler
//
// 核心设计：Template Method 模式
// Search() 定义主流程骨架，业务通过 override 虚函数定制各阶段逻辑：
//
//   CanSearch (含 InterposeCheckQuery)
//     → PreSearch
//     → DoSearch (召回)
//     → DoRank (排序)
//     → DoRerank (重排序)
//     → DoInterpose (人工干预: 封禁/仅出/过滤)
//     → SetResponse (组包)
//     → ReportFinal (上报)
//
// 返回值约定（
//   ret == 0  → 正常继续
//   ret < 0   → 中断请求，返回失败
//   ret > 0   → 中断请求，返回成功（空结果页）
// ============================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "framework/session/session.h"
#include "framework/processor/processor_pipeline.h"

namespace minisearchrec {
namespace framework {

// ============================================================
// 业务配置（对标 BusinessConfig protobuf）
// ============================================================
struct BusinessConfig {
    std::string business_type;      // 业务标识：search/sug/hint/nav
    std::string handler_name;       // Handler 类名（反射用）
    std::string session_name;       // Session 类名（反射用）
    bool skip_search = false;       // 是否跳过召回
    bool skip_rank = false;         // 是否跳过排序
    bool skip_interpose = false;    // 是否跳过干预
    std::unordered_map<std::string, std::string> extra_config;  // 业务自定义配置
};

// ============================================================
// 干预规则条目（对标 InterposeItem）
// ============================================================
struct InterposeRule {
    enum Action {
        BLOCK = 0,     // 封禁：从结果中删除
        FORCE_TOP = 1, // 仅出：强制插入到指定位置
        BOOST = 2,     // 提权：提高排序分数
        DEMOTE = 3,    // 降权：降低排序分数
    };
    std::string pattern;    // 匹配模式（精确匹配或正则）
    Action action = BLOCK;  // 干预动作
    int32_t position = -1;  // 仅出位置（FORCE_TOP 时有效，-1 表示追加）
    float weight = 0.0f;    // 权重（BOOST/DEMOTE 时有效）
    std::string reason;     // 干预原因
};

// ============================================================
// 干预结果记录（对标 InterposeSession::FilterItem）
// ============================================================
struct InterposeFilterRecord {
    std::string word;       // 被干预的词条
    std::string reason;     // 干预原因
    std::string ext_info;   // 扩展信息
};

// ============================================================
// BaseHandler
// ============================================================
class BaseHandler {
public:
    virtual ~BaseHandler() = default;

    // ── 初始化（服务启动时调用一次）──
    virtual int32_t Init(const BusinessConfig& config);

    // ── 主流程入口（每次请求调用）──
    // 模板方法，定义完整的请求处理 Pipeline
    virtual int32_t Search(Session* session) const;

    // ── 获取配置 ──
    const BusinessConfig& GetConfig() const { return config_; }
    virtual std::string HandlerName() const { return config_.handler_name; }

protected:
    // ================================================================
    // 各阶段虚函数（业务可覆写）
    // ================================================================

    // ── 0. InitSession ──
    virtual int32_t InitSession(Session* session) const;

    // ── 1. CanSearch 前置检查 ──
    // 包含 InterposeCheckQuery（query 级封禁）
    virtual bool CanSearch(Session* session) const;

    // query 级干预检查（对标 BaseHandler::InterposeCheckQuery）
    // 返回 false → query 被封禁，中断流程
    virtual bool InterposeCheckQuery(Session* session) const;

    // ── 2. PreSearch 阶段 ──
    virtual int32_t PreSearch(Session* session) const;
    virtual int32_t CommonPreSearch(Session* session) const;
    virtual int32_t ExtraPreSearch(Session* session) const;

    // ── 3. Search 阶段（召回）──
    virtual int32_t DoSearch(Session* session) const;
    virtual int32_t CommonDoSearch(Session* session) const;
    virtual int32_t ExtraDoSearch(Session* session) const;

    // ── 4. Rank 阶段（排序）──
    virtual int32_t DoRank(Session* session) const;
    virtual int32_t BeforeRank(Session* session) const;
    virtual int32_t CommonDoRank(Session* session) const;
    virtual int32_t ExtraDoRank(Session* session) const;
    virtual int32_t AfterRank(Session* session) const;

    // ── 5. Rerank 阶段（重排序，对标 BaseHandler::HandlerRerank）──
    //
    // 完整流程（
    //   a) SetRerankInput()      → 用 Rank output 构建 Rerank input
    //   b) CommonDoRerank()      → 调用 RankSvr 或本地 RankMgr 执行 Rerank
    //   c) AfterRerank()         → 后处理（结果替换、特征保留）
    //   (失败 → IsRerankFailUseRankOutput → 用 Rank 结果兜底)
    //
    // 使用场景：当粗排（Rank）无法满足精度需求时，对 Top-N 结果做二次精排
    // 例如：粗排用规则/轻量模型，Rerank 用 DeepFM/BERT 重打分
    virtual int32_t DoRerank(Session* session) const;

    // 构建 Rerank 输入（用 Rank 的 output 填充 Rerank 的 input）
    virtual int32_t SetRerankInput(Session* session) const;

    // 执行 Rerank 核心逻辑
    virtual int32_t CommonDoRerank(Session* session) const;

    // Rerank 后处理（结果替换 + 特征保留）
    virtual int32_t AfterRerank(Session* session) const;

    // Rerank 失败时是否使用 Rank 的结果兜底
    // 对标 IsRerankFailUseRankOutput
    virtual bool IsRerankFailUseRankOutput(Session* session) const { return false; }

    // ── 6. Interpose 阶段（人工干预，对标 BaseHandler::DoInterpose）──
    //
    // 干预分三步：
    //   a) GetInterposeRules()  → 获取干预规则（从配置/DB/远程加载）
    //   b) ApplyInterpose()    → 执行干预（封禁删除 + 仅出插入 + 提权降权）
    //   c) FilterInterposeOutput() → 干预后过滤（去除被干预的结果）
    //
    // 
    //   ExtraGetInterposeFeature → ExtraSetInterposeSession
    //   → 封禁过滤 → 仅出插入 → FilterInterposeOutput
    virtual int32_t DoInterpose(Session* session) const;

    // 获取干预规则列表（业务可覆写：从黑名单/配置/远程获取）
    virtual std::vector<InterposeRule> GetInterposeRules(Session* session) const;

    // 执行干预逻辑（封禁删除 + 仅出插入）
    virtual int32_t ApplyInterpose(Session* session,
                                   const std::vector<InterposeRule>& rules) const;

    // 干预后过滤（对标 FilterInterposeOutput）
    virtual void FilterInterposeOutput(Session* session) const;

    // 空结果是否需要干预（对标 EmptyRespNoNeedInterpose）
    // 返回 true → 空结果页不走干预
    virtual bool EmptyRespNoNeedInterpose(Session* session) const { return true; }

    // ── 7. SetResponse 阶段（组包）──
    virtual int32_t SetResponse(Session* session) const;
    virtual int32_t CommonSetResponse(Session* session) const;
    virtual int32_t ExtraSetResponse(Session* session) const;

    // ── 8. ReportFinal 阶段（上报/诊断）──
    virtual int32_t ReportFinal(Session* session) const;

    // ── 兜底 ──
    virtual void SetEmptyResponse(Session* session) const;
    virtual int32_t RankFallback(Session* session) const;

    // 额外初始化（Init 中调用）
    virtual int32_t ExtraInit();

protected:
    BusinessConfig config_;
};

} // namespace framework
} // namespace minisearchrec
