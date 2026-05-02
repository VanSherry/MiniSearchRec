// ============================================================
// MiniSearchRec - Sug 搜索建议 Handler
// 对标：通用搜索框架 SuggesterHandler（继承 BaseHandler）
// 走主流程框架：CanSearch→PreSearch→DoSearch(Trie召回)→DoRank(Processor链)→DoInterpose→SetResponse
// ============================================================

#pragma once

#include <string>
#include "framework/handler/base_handler.h"

namespace minisearchrec {

class SugBizHandler : public framework::BaseHandler {
public:
    static void RebuildTrie();

protected:
    std::string HandlerName() const override { return "SugBizHandler"; }

    // ── 框架各阶段实现 ──
    bool CanSearch(framework::Session* session) const override;
    int32_t DoSearch(framework::Session* session) const override;
    int32_t DoRank(framework::Session* session) const override;
    int32_t DoInterpose(framework::Session* session) const override;
    int32_t SetResponse(framework::Session* session) const override;

    // 业务初始化（框架在注册时自动调用，不需要 main.cpp 手动调）
    int32_t ExtraInit() override;
};

// 兼容旧接口名（保持 main.cpp 和 background_scheduler 不改）
using SugHandler = SugBizHandler;

} // namespace minisearchrec
