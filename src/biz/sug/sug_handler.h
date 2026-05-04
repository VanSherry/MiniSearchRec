#pragma once

#include <string>
#include "framework/handler/base_handler.h"

namespace minisearchrec {

class SugBizHandler : public framework::BaseHandler {
public:
    static void RebuildTrie();

protected:
    std::string HandlerName() const override { return "SugBizHandler"; }

    bool CanSearch(framework::Session* session) const override;
    int32_t MergeRecall(framework::Session* session,
                        const std::vector<framework::RecallOutputPtr>& outputs) const override;
    int32_t SetResponse(framework::Session* session) const override;
    int32_t ExtraInit() override;
};

using SugHandler = SugBizHandler;

} // namespace minisearchrec
