// ============================================================
// MiniSearchRec - 后台调度器
// 管理自动训练和自动索引重建的后台任务
// ============================================================

#ifndef MINISEARCHREC_BACKGROUND_SCHEDULER_H
#define MINISEARCHREC_BACKGROUND_SCHEDULER_H

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <memory>
#include <chrono>
#include <functional>

namespace minisearchrec {

// ============================================================
// 后台任务配置
// ============================================================
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

struct AutoIndexRebuildConfig {
    bool enable = false;
    int interval_hours = 12;
    int min_doc_changes = 10;
    int check_interval_sec = 600;
};

// ============================================================
// BackgroundScheduler - 单线程事件循环管理所有后台任务
// ============================================================
class BackgroundScheduler {
public:
    BackgroundScheduler() = default;
    ~BackgroundScheduler();

    // 配置
    void SetAutoTrainConfig(const AutoTrainConfig& cfg) { train_cfg_ = cfg; }
    void SetAutoIndexRebuildConfig(const AutoIndexRebuildConfig& cfg) { rebuild_cfg_ = cfg; }

    // 启动后台线程
    void Start();

    // 优雅停止（等待当前任务完成）
    void Stop();

    bool IsRunning() const { return running_.load(); }

private:
    void SchedulerLoop();

    // ── 自动训练 ──
    void CheckAndTrain();
    int CountEvents();         // 查询事件数量
    bool DumpTrainData();      // 导出训练样本
    bool RunTrainScript();     // fork 子进程训练
    bool HotReloadModel();     // 双 Buffer 热更新

    // ── 自动索引重建 ──
    void CheckAndRebuildIndex();
    bool RebuildIndexAtomically();  // 构建新索引 + 原子切换

    // 线程与同步
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::mutex cv_mutex_;
    std::condition_variable cv_;

    // 配置
    AutoTrainConfig train_cfg_;
    AutoIndexRebuildConfig rebuild_cfg_;

    // 状态追踪
    std::chrono::steady_clock::time_point last_train_time_;
    std::chrono::steady_clock::time_point last_rebuild_time_;
    int64_t last_train_event_count_ = 0;
    int doc_changes_since_rebuild_ = 0;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_BACKGROUND_SCHEDULER_H
