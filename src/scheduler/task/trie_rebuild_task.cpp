// ============================================================
// MiniSearchRec - Sug Trie 定时重建任务实现
// ============================================================

#include "scheduler/task/trie_rebuild_task.h"
#include "biz/sug/sug_handler.h"
#include "utils/logger.h"

namespace minisearchrec {
namespace scheduler {

void TrieRebuildTask::CheckAndRun() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_run_).count();

    if (elapsed_sec < cfg_.interval_sec) return;

    LOG_INFO("TrieRebuild: interval reached ({}s), rebuilding", elapsed_sec);
    SugHandler::RebuildTrie();
    last_run_ = std::chrono::steady_clock::now();
    LOG_INFO("TrieRebuild: complete");
}

} // namespace scheduler
} // namespace minisearchrec
