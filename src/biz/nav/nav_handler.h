// ============================================================
// MiniSearchRec - Nav 教育页（搜前引导）Handler
// 对标：通用搜索框架 NavHandler
// 各阶段：CanSearch→DoSearch(热词召回)→DoRank(NavScoreProcessor)→SetResponse
// ============================================================

#pragma once

#include <string>
#include "framework/handler/base_handler.h"

namespace minisearchrec {

class NavBizHandler : public framework::BaseHandler {
protected:
    std::string HandlerName() const override { return "NavBizHandler"; }

    bool CanSearch(framework::Session* session) const override;
    int32_t DoSearch(framework::Session* session) const override;
    int32_t DoRank(framework::Session* session) const override;
    int32_t SetResponse(framework::Session* session) const override;

    // 业务初始化（框架在注册时自动调用）
    int32_t ExtraInit() override;
};

using NavHandler = NavBizHandler;

} // namespace minisearchrec
