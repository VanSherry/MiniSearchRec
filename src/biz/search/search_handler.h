#pragma once

#include <string>
#include "framework/handler/base_handler.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class SearchBizHandler : public framework::BaseHandler {
protected:
    std::string HandlerName() const override { return "SearchBizHandler"; }

    // ── Session 转化（自定义，因为 Search 用 SearchSession）──
    SearchSession* GetSearchSession(framework::Session* session) const;

    // ── 自定义 Rank 输入输出（因为 Search 用 DocCandidate 向量而非 any_store）──
    std::vector<rank::RankItem> BuildRankInput(
        framework::Session* session, const std::string& stage) const override;
    void ApplyRankOutput(framework::Session* session,
                         std::vector<rank::RankItem>& items,
                         const std::string& stage) const override;

    // ── Hook 覆写 ──
    bool CanSearch(framework::Session* session) const override;
    int32_t PreSearch(framework::Session* session) const override;
    int32_t ExtraPreSearch(framework::Session* session) const override;
    int32_t MergeRecall(framework::Session* session,
                        const std::vector<framework::RecallOutputPtr>& outputs) const override;
    int32_t AfterRank(framework::Session* session) const override;
    int32_t AfterRerank(framework::Session* session) const override;
    int32_t DoInterpose(framework::Session* session) const override;
    int32_t SetResponse(framework::Session* session) const override;
    void Report(framework::Session* session) const override;
    int32_t ExtraInit() override;
};

using SearchHandler = SearchBizHandler;

// 精排模型热更新接口
int ReloadRankModel(const std::string& new_model_path);

} // namespace minisearchrec
