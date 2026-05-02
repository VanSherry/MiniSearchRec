// ============================================================
// MiniSearchRec - 垂搜 Handler（biz 层）
// 对标：通用搜索框架，垂搜作为一个 business 接入
//
// 彻底走 framework::BaseHandler 主流程 + 框架 ProcessorPipeline：
//   PreSearch  → QP 理解 + AB 实验
//   DoSearch   → 框架自动调度 recall_pipeline（从 recall.yaml 配置）
//   DoRank     → 框架自动调度 rank_pipeline（从 rank.yaml 配置）
//   DoRerank   → 框架自动调度 rerank_pipeline（从 rank.yaml 配置）
//   DoInterpose → 框架自动调度 filter_pipeline + postprocess_pipeline
//   SetResponse → 分页 + JSON 序列化
//
// 去掉了旧的：
//   - SearchRank / SearchContext / SearchFactory 适配层
//   - core/pipeline.h（Pipeline 编排器）
//   - core/factory.h（手动注册）
// ============================================================

#pragma once

#include <string>
#include "framework/handler/base_handler.h"
#include "framework/class_register.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class SearchBizHandler : public framework::BaseHandler {
protected:
    std::string HandlerName() const override { return "SearchBizHandler"; }

    // ── 框架各阶段覆写 ──

    // PreSearch：QP 理解 + AB 实验染色
    int32_t ExtraPreSearch(framework::Session* session) const override;

    // DoSearch：框架默认调度 recall_pipeline，
    //           这里额外做多路并行合并（override CommonDoSearch）
    int32_t CommonDoSearch(framework::Session* session) const override;

    // DoRank：框架默认调度 rank_pipeline，
    //         这里额外做粗排截断
    int32_t AfterRank(framework::Session* session) const override;

    // DoRerank：框架默认调度 rerank_pipeline，
    //           这里额外做精排后截断
    int32_t AfterRerank(framework::Session* session) const override;

    // DoInterpose：框架默认 + filter/postprocess pipeline
    int32_t DoInterpose(framework::Session* session) const override;

    // SetResponse：分页 + JSON 序列化
    int32_t SetResponse(framework::Session* session) const override;

    int32_t ExtraInit() override;

private:
    // 辅助：向下转型获取 SearchSession
    static SearchSession* GetSearchSession(framework::Session* s);
};

using BizSearchHandler = SearchBizHandler;

// 模型热更新（通过框架 PipelineManager 实现）
int ReloadRankModel(const std::string& new_model_path);

} // namespace minisearchrec
