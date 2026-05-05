// ============================================================
// MiniSearchRec - 自动索引重建后台任务
// 流程：全量重建新索引 → 原子切换 → 持久化
// ============================================================

#pragma once

#include "scheduler/scheduler.h"
#include <chrono>

namespace minisearchrec {
namespace scheduler {

struct IndexRebuildConfig {
    bool enable = false;
    int interval_hours = 12;
    int min_doc_changes = 10;
    int check_interval_sec = 600;
};

class IndexRebuildTask : public BackgroundTask {
public:
    explicit IndexRebuildTask(const IndexRebuildConfig& cfg) : cfg_(cfg) {}

    std::string Name() const override { return "IndexRebuild"; }
    int CheckIntervalSec() const override { return cfg_.check_interval_sec; }
    bool IsEnabled() const override { return cfg_.enable; }
    void CheckAndRun() override;

    // 立即触发全量重建（用于管理后台手动触发）
    bool RebuildAtomically();

    int64_t LastRunEpoch() const override { return last_run_epoch_; }

private:
    IndexRebuildConfig cfg_;
    std::chrono::steady_clock::time_point last_run_ = std::chrono::steady_clock::now();
    int64_t last_run_epoch_ = 0;
};

} // namespace scheduler
} // namespace minisearchrec
