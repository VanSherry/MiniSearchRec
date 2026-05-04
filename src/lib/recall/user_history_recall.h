// ============================================================
// MiniSearchRec - 用户历史召回处理器
// 参考：X(Twitter) UTEG (User Tweet Entity Graph)（DAG 并行模式）
// ============================================================

#ifndef MINISEARCHREC_USER_HISTORY_RECALL_H
#define MINISEARCHREC_USER_HISTORY_RECALL_H

#include "framework/processor/dag_pipeline.h"
#include "biz/search/search_session.h"
#include <unordered_map>
#include <string>

namespace minisearchrec {

class UserHistoryRecallProcessor : public BaseRecallProcessor {
public:
    UserHistoryRecallProcessor() = default;
    ~UserHistoryRecallProcessor() override = default;

    int ProcessDag(framework::DagProcessorContext* ctx) override;
    std::string Name() const override { return "UserHistoryRecallProcessor"; }
    int Init(const YAML::Node& config) override;

private:
    int max_recall_ = 200;
    int history_window_days_ = 30;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_USER_HISTORY_RECALL_H
