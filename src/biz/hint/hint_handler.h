// ============================================================
// MiniSearchRec - Hint 相关搜索（点后推）Handler
// 对标：通用搜索框架 ClickHintHandler（继承 BaseHandler）
// 各阶段：CanSearch→DoSearch(四路召回)→DoRank(Processor链)→DoInterpose→SetResponse
// ============================================================

#pragma once

#include <string>
#include "framework/handler/base_handler.h"

namespace minisearchrec {

class HintBizHandler : public framework::BaseHandler {
protected:
    std::string HandlerName() const override { return "HintBizHandler"; }

    bool CanSearch(framework::Session* session) const override;
    int32_t DoSearch(framework::Session* session) const override;
    int32_t DoRank(framework::Session* session) const override;
    int32_t DoInterpose(framework::Session* session) const override;
    int32_t SetResponse(framework::Session* session) const override;

    // 业务初始化（框架在注册时自动调用）
    int32_t ExtraInit() override;
};

using HintHandler = HintBizHandler;

} // namespace minisearchrec
