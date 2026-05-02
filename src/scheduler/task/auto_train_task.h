// ============================================================
// MiniSearchRec - 自动训练后台任务
// 流程：事件计数 → dump 样本 → 训练模型 → 热更新
// ============================================================

#pragma once

#include "scheduler/scheduler.h"
#include <chrono>
#include <string>

namespace minisearchrec {
namespace scheduler {

struct AutoTrainConfig {
    bool enable = false;
    int interval_hours = 24;
    int min_events = 500;
    int check_interval_sec = 300;
    std::string train_script = "./scripts/train_rank_model.py";
    std::string model_output = "./models/rank_model.txt";
    std::string events_db = "./data/events.db";
    std::string docs_db = "./data/docs.db";
    std::string train_data_output = "./data/train.txt";
    std::string dump_tool = "./build/dump_train_data";
};

class AutoTrainTask : public BackgroundTask {
public:
    explicit AutoTrainTask(const AutoTrainConfig& cfg) : cfg_(cfg) {}

    std::string Name() const override { return "AutoTrain"; }
    int CheckIntervalSec() const override { return cfg_.check_interval_sec; }
    bool IsEnabled() const override { return cfg_.enable; }
    void CheckAndRun() override;

private:
    int CountEvents();
    bool DumpTrainData();
    bool RunTrainScript();
    bool HotReloadModel();

    AutoTrainConfig cfg_;
    std::chrono::steady_clock::time_point last_run_ = std::chrono::steady_clock::now();
    int64_t last_event_count_ = 0;
};

} // namespace scheduler
} // namespace minisearchrec
