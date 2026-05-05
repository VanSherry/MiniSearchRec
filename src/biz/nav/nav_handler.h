#pragma once

#include <string>
#include "framework/handler/base_handler.h"

namespace minisearchrec {

class NavBizHandler : public framework::BaseHandler {
protected:
    std::string HandlerName() const override { return "NavBizHandler"; }

    bool CanSearch(framework::Session* session) const override;
    int32_t MergeRecall(framework::Session* session,
                        const std::vector<framework::RecallOutputPtr>& outputs) const override;
    int32_t SetResponse(framework::Session* session) const override;
    void Report(framework::Session* session) const override;
    int32_t ExtraInit() override;
};

using NavHandler = NavBizHandler;

} // namespace minisearchrec
