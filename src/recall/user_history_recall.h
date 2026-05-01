// ============================================================
// MiniSearchRec - 用户历史召回处理器
// 参考：X(Twitter) UTEG (User Tweet Entity Graph)
// ============================================================

#ifndef MINISEARCHREC_USER_HISTORY_RECALL_H
#define MINISEARCHREC_USER_HISTORY_RECALL_H

#include "core/processor.h"
#include <unordered_map>
#include <string>

namespace minisearchrec {

class UserHistoryRecallProcessor : public BaseRecallProcessor {
public:
    UserHistoryRecallProcessor() = default;
    ~UserHistoryRecallProcessor() override = default;

    int Process(Session& session) override;
    std::string Name() const override { return "UserHistoryRecallProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    int max_recall_ = 200;
    int history_window_days_ = 30;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_USER_HISTORY_RECALL_H
