// ============================================================
// MiniSearchRec - DAG 线程池
// 全局共享线程池，FIFO 公平调度
// 高并发场景下不会因为单个请求的大 DAG 饿死其他请求
// ============================================================

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace minisearchrec {
namespace framework {

class DagThreadPool {
public:
    static DagThreadPool& Instance() {
        static DagThreadPool inst;
        return inst;
    }

    // 初始化线程池（服务启动时调用一次）
    void Init(size_t thread_count) {
        if (started_.load()) return;
        started_.store(true);
        stop_.store(false);

        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this]() { WorkerLoop(); });
        }
    }

    // 关闭线程池
    void Shutdown() {
        if (!started_.load()) return;
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_.store(true);
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
        started_.store(false);
    }

    // 提交任务到线程池
    template <typename F>
    void Submit(F&& func) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            tasks_.push(std::function<void()>(std::forward<F>(func)));
        }
        cv_.notify_one();
    }

    size_t ThreadCount() const { return workers_.size(); }
    bool IsStarted() const { return started_.load(); }

private:
    DagThreadPool() = default;
    ~DagThreadPool() { Shutdown(); }
    DagThreadPool(const DagThreadPool&) = delete;
    DagThreadPool& operator=(const DagThreadPool&) = delete;

    void WorkerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this]() { return stop_.load() || !tasks_.empty(); });
                if (stop_.load() && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> started_{false};
};

}  // namespace framework
}  // namespace minisearchrec
