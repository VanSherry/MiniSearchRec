// ============================================================
// MiniSearchRec - 自动训练任务实现
// ============================================================

#include "scheduler/task/auto_train_task.h"
#include "framework/config/config_manager.h"
#include "utils/logger.h"

#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <sqlite3.h>

namespace minisearchrec {
namespace scheduler {

void AutoTrainTask::CheckAndRun() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - last_run_).count();

    if (elapsed < cfg_.interval_hours) {
        int event_count = CountEvents();
        if (event_count - last_event_count_ < cfg_.min_events) {
            return;
        }
        LOG_INFO("AutoTrain: event threshold reached ({} new events), triggering early",
                 event_count - last_event_count_);
    } else {
        LOG_INFO("AutoTrain: interval reached ({}h elapsed), triggering", elapsed);
    }

    LOG_INFO("AutoTrain: starting dump → train → reload pipeline");

    if (!DumpTrainData()) {
        LOG_ERROR("AutoTrain: dump failed, skipping this cycle");
        return;
    }

    if (!RunTrainScript()) {
        LOG_ERROR("AutoTrain: train script failed, skipping reload");
        return;
    }

    if (!HotReloadModel()) {
        LOG_ERROR("AutoTrain: hot reload failed, old model still active");
    } else {
        LOG_INFO("AutoTrain: pipeline complete, new model active");
    }

    last_run_ = std::chrono::steady_clock::now();
    last_event_count_ = CountEvents();
}

int AutoTrainTask::CountEvents() {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(cfg_.events_db.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
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

bool AutoTrainTask::DumpTrainData() {
    std::string cmd = cfg_.dump_tool
        + " --events-db " + cfg_.events_db
        + " --docs-db " + cfg_.docs_db
        + " --output " + cfg_.train_data_output;

    LOG_INFO("AutoTrain: executing dump: {}", cmd);

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) { execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr); _exit(127); }

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool AutoTrainTask::RunTrainScript() {
    std::ifstream f(cfg_.train_data_output);
    if (!f.good()) {
        LOG_WARN("AutoTrain: training data not found: {}", cfg_.train_data_output);
        return false;
    }
    f.close();

    std::string cmd = "python3 " + cfg_.train_script
        + " --input " + cfg_.train_data_output
        + " --output " + cfg_.model_output
        + " --incremental";

    LOG_INFO("AutoTrain: executing train: {}", cmd);

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) { execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr); _exit(127); }

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool AutoTrainTask::HotReloadModel() {
    std::ifstream f(cfg_.model_output);
    if (!f.good()) return false;
    f.close();

    std::string reload_cmd = "curl -s -X POST http://127.0.0.1:"
        + std::to_string(ConfigManager::Instance().GetGlobalConfig().server.port)
        + "/api/v1/admin/reload_model -d '{\"model_path\":\""
        + cfg_.model_output + "\"}'";

    LOG_INFO("AutoTrain: triggering hot reload via: {}", reload_cmd);
    return system(reload_cmd.c_str()) == 0;
}

} // namespace scheduler
} // namespace minisearchrec
