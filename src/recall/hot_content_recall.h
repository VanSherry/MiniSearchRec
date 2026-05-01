// ============================================================
// MiniSearchRec - 热门内容召回处理器
// 推荐近期热门（高点击/高点赞）内容
// 优化：维护定期刷新的热榜缓存，避免每次全表扫描
// ============================================================

#ifndef MINISEARCHREC_HOT_CONTENT_RECALL_H
#define MINISEARCHREC_HOT_CONTENT_RECALL_H

#include "core/processor.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

namespace minisearchrec {

struct HotItem {
    std::string doc_id;
    float score = 0.0f;
    int64_t click_count = 0;
    int64_t like_count = 0;
    int64_t publish_time = 0;
    std::string title;
    std::string author;
    float quality_score = 0.0f;
};

class HotContentRecallProcessor : public BaseRecallProcessor {
public:
    HotContentRecallProcessor() = default;
    ~HotContentRecallProcessor() override = default;

    int Process(Session& session) override;
    std::string Name() const override { return "HotContentRecallProcessor"; }
    bool Init(const YAML::Node& config) override;

    // 刷新热榜缓存（可由定时线程调用）
    void RefreshHotList();

private:
    int max_recall_ = 100;
    int time_window_hours_ = 24;
    // 热榜缓存刷新间隔（秒），默认 5 分钟
    int refresh_interval_sec_ = 300;

    // 热榜快照（读写分离，mutex 保护）
    mutable std::mutex hot_list_mutex_;
    std::vector<HotItem> hot_list_cache_;
    std::atomic<int64_t> last_refresh_time_{0};
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HOT_CONTENT_RECALL_H
