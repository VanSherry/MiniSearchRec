// ============================================================
// MiniSearchRec - Sug Trie 定时重建后台任务
// ============================================================

#pragma once

#include "scheduler/scheduler.h"
#include <chrono>

namespace minisearchrec {
namespace scheduler {

struct TrieRebuildConfig {
    bool enable = true;
    int interval_sec = 3600;
};

class TrieRebuildTask : public BackgroundTask {
public:
    explicit TrieRebuildTask(const TrieRebuildConfig& cfg) : cfg_(cfg) {}

    std::string Name() const override { return "TrieRebuild"; }
    int CheckIntervalSec() const override { return cfg_.interval_sec; }
    bool IsEnabled() const override { return cfg_.enable; }
    void CheckAndRun() override;

private:
    TrieRebuildConfig cfg_;
    std::chrono::steady_clock::time_point last_run_ = std::chrono::steady_clock::now();
};

} // namespace scheduler
} // namespace minisearchrec
