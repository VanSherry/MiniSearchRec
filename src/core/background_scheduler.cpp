// ============================================================
// MiniSearchRec - 后台调度器实现
// ============================================================

#include "core/background_scheduler.h"
#include "core/app_context.h"
#include "core/config_manager.h"
#include "index/inverted_index.h"
#include "index/vector_index.h"
#include "index/doc_store.h"
#include "index/index_builder.h"
#include "utils/logger.h"

#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <sqlite3.h>

namespace minisearchrec {

BackgroundScheduler::~BackgroundScheduler() {
    Stop();
}

void BackgroundScheduler::Start() {
    if (running_.load()) return;

    stop_requested_.store(false);
    running_.store(true);
    last_train_time_ = std::chrono::steady_clock::now();
    last_rebuild_time_ = std::chrono::steady_clock::now();

    worker_ = std::thread(&BackgroundScheduler::SchedulerLoop, this);

    LOG_INFO("BackgroundScheduler started: train={}, rebuild={}",
             train_cfg_.enable, rebuild_cfg_.enable);
}

void BackgroundScheduler::Stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false);
    LOG_INFO("BackgroundScheduler stopped.");
}

// ============================================================
// 主事件循环
// ============================================================
void BackgroundScheduler::SchedulerLoop() {
    // 使用两个配置中较小的 check_interval 作为唤醒间隔
    int wake_interval_sec = 60;
    if (train_cfg_.enable) {
        wake_interval_sec = std::min(wake_interval_sec, train_cfg_.check_interval_sec);
    }
    if (rebuild_cfg_.enable) {
        wake_interval_sec = std::min(wake_interval_sec, rebuild_cfg_.check_interval_sec);
    }
    wake_interval_sec = std::max(wake_interval_sec, 10);  // 最短 10s

    LOG_INFO("BackgroundScheduler: loop started, wake_interval={}s", wake_interval_sec);

    while (!stop_requested_.load()) {
        // 等待唤醒
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::seconds(wake_interval_sec),
                         [this]() { return stop_requested_.load(); });
        }

        if (stop_requested_.load()) break;

        // 检查自动训练
        if (train_cfg_.enable) {
            CheckAndTrain();
        }

        // 检查自动索引重建
        if (rebuild_cfg_.enable) {
            CheckAndRebuildIndex();
        }
    }
}

// ============================================================
// 自动训练
// ============================================================
void BackgroundScheduler::CheckAndTrain() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
        now - last_train_time_).count();

    // 检查是否达到时间间隔
    if (elapsed < train_cfg_.interval_hours) {
        // 未到时间，检查是否事件数满足提前触发
        int event_count = CountEvents();
        if (event_count - last_train_event_count_ < train_cfg_.min_events) {
            return;  // 事件不够，跳过
        }
        LOG_INFO("AutoTrain: event threshold reached ({} new events), triggering early",
                 event_count - last_train_event_count_);
    } else {
        LOG_INFO("AutoTrain: interval reached ({}h elapsed), triggering",
                 elapsed);
    }

    // 执行训练流程
    LOG_INFO("AutoTrain: starting dump → train → reload pipeline");

    // Step 1: dump 训练样本
    if (!DumpTrainData()) {
        LOG_ERROR("AutoTrain: dump_train_data failed, skipping this cycle");
        return;
    }

    // Step 2: fork 子进程执行训练
    if (!RunTrainScript()) {
        LOG_ERROR("AutoTrain: train script failed, skipping reload");
        return;
    }

    // Step 3: 热更新模型
    if (!HotReloadModel()) {
        LOG_ERROR("AutoTrain: hot reload failed, old model still active");
    } else {
        LOG_INFO("AutoTrain: pipeline complete, new model active");
    }

    // 更新状态
    last_train_time_ = std::chrono::steady_clock::now();
    last_train_event_count_ = CountEvents();
}

int BackgroundScheduler::CountEvents() {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(train_cfg_.events_db.c_str(), &db,
                        SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        return 0;
    }

    const char* sql = "SELECT COUNT(*) FROM search_events";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return count;
}

bool BackgroundScheduler::DumpTrainData() {
    // 使用 fork+exec 执行 dump_train_data 工具（进程隔离）
    std::string cmd = train_cfg_.dump_tool
        + " --events-db " + train_cfg_.events_db
        + " --docs-db " + train_cfg_.docs_db
        + " --output " + train_cfg_.train_data_output;

    LOG_INFO("AutoTrain: executing dump: {}", cmd);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("AutoTrain: fork failed for dump");
        return false;
    }

    if (pid == 0) {
        // 子进程
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // 父进程等待
    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        LOG_INFO("AutoTrain: dump completed successfully");
        return true;
    }

    LOG_ERROR("AutoTrain: dump failed with exit code {}",
              WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return false;
}

bool BackgroundScheduler::RunTrainScript() {
    // 检查训练数据是否存在
    std::ifstream f(train_cfg_.train_data_output);
    if (!f.good()) {
        LOG_WARN("AutoTrain: training data not found: {}",
                 train_cfg_.train_data_output);
        return false;
    }
    f.close();

    // fork 子进程执行 Python 训练脚本（进程隔离，崩溃不影响主服务）
    std::string cmd = "python3 " + train_cfg_.train_script
        + " --input " + train_cfg_.train_data_output
        + " --output " + train_cfg_.model_output
        + " --incremental";

    LOG_INFO("AutoTrain: executing train: {}", cmd);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("AutoTrain: fork failed for training");
        return false;
    }

    if (pid == 0) {
        // 子进程
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // 父进程等待（训练可能耗时，但在后台线程不阻塞 HTTP 服务）
    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        LOG_INFO("AutoTrain: training completed successfully");
        return true;
    }

    LOG_ERROR("AutoTrain: training failed with exit code {}",
              WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return false;
}

bool BackgroundScheduler::HotReloadModel() {
    // 检查模型文件是否存在
    std::ifstream f(train_cfg_.model_output);
    if (!f.good()) {
        LOG_WARN("AutoTrain: model file not found: {}", train_cfg_.model_output);
        return false;
    }
    f.close();

    // 通过 SearchHandler 的 Pipeline 触发双 Buffer 热更新
    // 直接调用 AppContext 中 Pipeline 的 HotReload 接口
    // 注：这里需要获取 Pipeline 实例，通过已有的 search_handler 机制
    // 实际走 HTTP 内部调用更解耦，但这里直接调用更高效
    auto& ctx = AppContext::Instance();

    // Pipeline 是在 SearchHandler 内创建的 singleton
    // 通过触发 reload_model API 的逻辑来实现
    // 简化实现：直接通过文件标记通知主线程 reload
    // 或者直接用已有的 Pipeline::HotReloadFineScorer

    // 写一个 reload marker 文件，让主服务定期检查（最简单的解耦方式）
    // 但更优的做法是直接调用 Pipeline 的 HotReload
    // 由于 Pipeline 是 SearchHandler 内的 static singleton，这里用全局访问

    // 使用外部 HTTP 调用自身 reload 接口（最解耦）
    std::string reload_cmd = "curl -s -X POST http://127.0.0.1:"
        + std::to_string(ConfigManager::Instance().GetGlobalConfig().server.port)
        + "/api/v1/admin/reload_model -d '{\"model_path\":\""
        + train_cfg_.model_output + "\"}'";

    LOG_INFO("AutoTrain: triggering hot reload via: {}", reload_cmd);

    int ret = system(reload_cmd.c_str());
    if (ret == 0) {
        LOG_INFO("AutoTrain: hot reload triggered successfully");
        return true;
    }

    LOG_ERROR("AutoTrain: hot reload request failed, ret={}", ret);
    return false;
}

// ============================================================
// 自动索引重建
// ============================================================
void BackgroundScheduler::CheckAndRebuildIndex() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
        now - last_rebuild_time_).count();

    if (elapsed < rebuild_cfg_.interval_hours) {
        return;  // 未到时间
    }

    LOG_INFO("AutoIndexRebuilder: interval reached ({}h), starting rebuild", elapsed);

    if (RebuildIndexAtomically()) {
        LOG_INFO("AutoIndexRebuilder: rebuild and swap complete");
        last_rebuild_time_ = std::chrono::steady_clock::now();
    } else {
        LOG_ERROR("AutoIndexRebuilder: rebuild failed, keeping old index");
    }
}

bool BackgroundScheduler::RebuildIndexAtomically() {
    auto& ctx = AppContext::Instance();
    auto doc_store = ctx.GetDocStore();

    if (!doc_store) {
        LOG_ERROR("AutoIndexRebuilder: DocStore not available");
        return false;
    }

    // Step 1: 读取全量文档 ID
    auto all_ids = doc_store->GetAllDocIds();
    if (all_ids.empty()) {
        LOG_WARN("AutoIndexRebuilder: no documents in DocStore, skipping");
        return false;
    }

    LOG_INFO("AutoIndexRebuilder: rebuilding from {} documents", all_ids.size());

    // Step 2: 构建新的倒排索引和向量索引（在独立内存中）
    auto new_inverted = std::make_shared<InvertedIndex>();

    auto emb_provider = ctx.GetEmbeddingProvider();
    VectorIndexConfig vec_cfg;
    vec_cfg.dim = emb_provider->GetDim();
    auto new_vector = std::make_shared<VectorIndex>(vec_cfg);

    for (const auto& doc_id : all_ids) {
        Document doc;
        if (!doc_store->GetDoc(doc_id, doc)) continue;

        // 倒排索引
        std::vector<std::string> tags;
        for (int i = 0; i < doc.tags_size(); ++i) {
            tags.push_back(doc.tags(i));
        }
        new_inverted->AddDocument(
            doc.doc_id(), doc.title(), doc.content(),
            doc.category(), tags, doc.content_length());

        // 向量索引
        if (emb_provider) {
            auto emb = emb_provider->Encode(doc.title() + " " + doc.content());
            new_vector->AddVector(doc.doc_id(), emb);
        }
    }

    LOG_INFO("AutoIndexRebuilder: new index built, docs={}, vectors={}",
             new_inverted->GetDocCount(), new_vector->GetVectorCount());

    // Step 3: 原子切换（通过 AppContext 的 swap 接口）
    ctx.SwapIndexes(new_inverted, new_vector);

    // Step 4: 持久化新索引到磁盘
    const auto& index_dir = ConfigManager::Instance().GetGlobalConfig().index.index_dir;
    new_inverted->Save(index_dir + "/inverted.idx");
    new_vector->Save(index_dir + "/vector.faiss");

    LOG_INFO("AutoIndexRebuilder: new indexes saved to disk");
    return true;
}

} // namespace minisearchrec
