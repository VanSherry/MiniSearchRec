#ifndef MINISEARCHREC_SEARCH_CONTEXT_H
#define MINISEARCHREC_SEARCH_CONTEXT_H

#include "lib/rank/base/rank_context.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

// 搜索排序上下文
class SearchContext : public rank::RankContext {
public:
    std::string stage;  // "coarse" | "fine"（由 SearchRank::PrepareInput 设置）

    // 便捷访问 SearchSession
    SearchSession* Session() const {
        return dynamic_cast<SearchSession*>(session_);
    }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SEARCH_CONTEXT_H
