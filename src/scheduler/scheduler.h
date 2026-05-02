// ============================================================
// MiniSearchRec - 后台任务调度器
// 职责：管理所有后台定时任务的注册、调度、停止
//
// 配置驱动：Scheduler::InitFromConfig(yaml_path)
// 从 framework.yaml 的 background 段自动读取所有任务参数
// ============================================================

#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <chrono>
#include <string>

namespace minisearchrec {
namespace scheduler {

// ============================================================
// BackgroundTask 接口：所有后台任务继承此基类
// ============================================================
class BackgroundTask {
public:
    virtual ~BackgroundTask() = default;

    // 任务名称（用于日志）
    virtual std::string Name() const = 0;

    // 检查间隔（秒）
    virtual int CheckIntervalSec() const = 0;

    // 是否启用
    virtual bool IsEnabled() const = 0;

    // 执行检查：判断是否需要执行 + 执行
    virtual void CheckAndRun() = 0;
};

using TaskPtr = std::shared_ptr<BackgroundTask>;

// ============================================================
// Scheduler：单线程事件循环，管理所有 BackgroundTask
// ============================================================
class Scheduler {
public:
    Scheduler() = default;
    ~Scheduler();

    // 配置驱动初始化（推荐）：从 YAML 的 background 段自动创建所有 Task
    // yaml_path 指向 framework.yaml
    bool InitFromConfig(const std::string& yaml_path);

    // 手动注册任务（编程式）
    void AddTask(TaskPtr task);

    // 启动调度线程
    void Start();

    // 优雅停止
    void Stop();

    bool IsRunning() const { return running_.load(); }
    size_t TaskCount() const { return tasks_.size(); }

private:
    void Loop();

    std::vector<TaskPtr> tasks_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

} // namespace scheduler
} // namespace minisearchrec
