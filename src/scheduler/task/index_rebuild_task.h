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

private:
    bool RebuildAtomically();

    IndexRebuildConfig cfg_;
    std::chrono::steady_clock::time_point last_run_ = std::chrono::steady_clock::now();
};

} // namespace scheduler
} // namespace minisearchrec
