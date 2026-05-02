// ============================================================
// MiniSearchRec - 后台任务调度器实现
// ============================================================

#include "scheduler/scheduler.h"
#include "scheduler/task/auto_train_task.h"
#include "scheduler/task/index_rebuild_task.h"
#include "scheduler/task/trie_rebuild_task.h"
#include "utils/logger.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>

namespace minisearchrec {
namespace scheduler {

Scheduler::~Scheduler() {
    Stop();
}

// ============================================================
// InitFromConfig：从 framework.yaml 的 background 段创建所有 Task
// ============================================================
bool Scheduler::InitFromConfig(const std::string& yaml_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Scheduler::InitFromConfig: failed to load '{}': {}", yaml_path, e.what());
        return false;
    }

    auto bg = root["background"];
    if (!bg || bg.IsNull()) {
        LOG_WARN("Scheduler: no 'background' section in config, no tasks loaded");
        return true;
    }

    // ── auto_train ──
    if (bg["auto_train"]) {
        auto node = bg["auto_train"];
        AutoTrainConfig cfg;
        cfg.enable            = node["enable"].as<bool>(false);
        cfg.interval_hours    = node["interval_hours"].as<int>(24);
        cfg.min_events        = node["min_events"].as<int>(500);
        cfg.check_interval_sec = node["check_interval_sec"].as<int>(300);
        cfg.train_script      = node["train_script"].as<std::string>("./scripts/train_rank_model.py");
        cfg.model_output      = node["model_output"].as<std::string>("./models/rank_model.txt");
        cfg.events_db         = node["events_db"].as<std::string>("./data/events.db");
        cfg.docs_db           = node["docs_db"].as<std::string>("./data/docs.db");
        cfg.train_data_output = node["train_data_output"].as<std::string>("./data/train.txt");
        cfg.dump_tool         = node["dump_tool"].as<std::string>("./build/dump_train_data");
        AddTask(std::make_shared<AutoTrainTask>(cfg));
        LOG_INFO("Scheduler: auto_train task loaded (enable={}, interval={}h)",
                 cfg.enable, cfg.interval_hours);
    }

    // ── auto_index_rebuild ──
    if (bg["auto_index_rebuild"]) {
        auto node = bg["auto_index_rebuild"];
        IndexRebuildConfig cfg;
        cfg.enable             = node["enable"].as<bool>(false);
        cfg.interval_hours     = node["interval_hours"].as<int>(12);
        cfg.min_doc_changes    = node["min_doc_changes"].as<int>(10);
        cfg.check_interval_sec = node["check_interval_sec"].as<int>(600);
        AddTask(std::make_shared<IndexRebuildTask>(cfg));
        LOG_INFO("Scheduler: auto_index_rebuild task loaded (enable={}, interval={}h)",
                 cfg.enable, cfg.interval_hours);
    }

    // ── sug_trie_rebuild ──
    if (bg["sug_trie_rebuild"]) {
        auto node = bg["sug_trie_rebuild"];
        TrieRebuildConfig cfg;
        cfg.enable       = node["enable"].as<bool>(true);
        cfg.interval_sec = node["interval_sec"].as<int>(3600);
        AddTask(std::make_shared<TrieRebuildTask>(cfg));
        LOG_INFO("Scheduler: sug_trie_rebuild task loaded (enable={}, interval={}s)",
                 cfg.enable, cfg.interval_sec);
    }

    LOG_INFO("Scheduler: {} tasks loaded from config", tasks_.size());
    return true;
}

void Scheduler::AddTask(TaskPtr task) {
    tasks_.push_back(std::move(task));
}

void Scheduler::Start() {
    if (running_.load()) return;

    stop_requested_.store(false);
    running_.store(true);
    worker_ = std::thread(&Scheduler::Loop, this);

    LOG_INFO("Scheduler started with {} tasks", tasks_.size());
}

void Scheduler::Stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false);
    LOG_INFO("Scheduler stopped.");
}

void Scheduler::Loop() {
    // 计算最小检查间隔作为唤醒频率
    int wake_sec = 60;
    for (auto& task : tasks_) {
        if (task->IsEnabled()) {
            wake_sec = std::min(wake_sec, task->CheckIntervalSec());
        }
    }
    wake_sec = std::max(wake_sec, 10);

    LOG_INFO("Scheduler: loop started, wake_interval={}s", wake_sec);

    while (!stop_requested_.load()) {
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::seconds(wake_sec),
                         [this]() { return stop_requested_.load(); });
        }

        if (stop_requested_.load()) break;

        for (auto& task : tasks_) {
            if (task->IsEnabled()) {
                task->CheckAndRun();
            }
            if (stop_requested_.load()) break;
        }
    }
}

} // namespace scheduler
} // namespace minisearchrec
