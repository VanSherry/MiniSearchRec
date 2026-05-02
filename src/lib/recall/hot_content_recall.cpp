// ============================================================
// MiniSearchRec - 热门内容召回处理器实现
// 推荐近期热门（高点击/高点赞）内容
// 优化：维护定期刷新的热榜缓存，避免每次全表扫描
// ============================================================

#include "lib/recall/hot_content_recall.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace minisearchrec {

bool HotContentRecallProcessor::Init(const YAML::Node& config) {
    if (config["enable"]) {
        enabled_ = config["enable"].as<bool>(true);
    }
    if (config["max_recall"]) {
        max_recall_ = config["max_recall"].as<int>(100);
    }
    if (config["time_window_hours"]) {
        time_window_hours_ = config["time_window_hours"].as<int>(24);
    }
    if (config["refresh_interval_sec"]) {
        refresh_interval_sec_ = config["refresh_interval_sec"].as<int>(300);
    }
    return true;
}

void HotContentRecallProcessor::RefreshHotList() {
    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) return;

    auto all_ids = doc_store->GetAllDocIds();
    if (all_ids.empty()) return;

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t time_window_sec = static_cast<int64_t>(time_window_hours_) * 3600;

    std::vector<HotItem> new_list;
    new_list.reserve(std::min((int)all_ids.size(), max_recall_ * 5));
    size_t new_size = 0;

    for (const auto& doc_id : all_ids) {
        Document doc;
        if (!doc_store->GetDoc(doc_id, doc)) continue;

        int64_t age = now - doc.publish_time();
        if (time_window_hours_ > 0 && age > time_window_sec) continue;

        float hot_score = std::log1pf(static_cast<float>(doc.click_count())) * 0.6f
                        + std::log1pf(static_cast<float>(doc.like_count()))  * 0.4f;
        new_list.push_back({doc_id, hot_score,
                            doc.click_count(), doc.like_count(),
                            doc.publish_time(), doc.title(),
                            doc.author(), doc.quality_score()});
    }

    std::sort(new_list.begin(), new_list.end(),
              [](const HotItem& a, const HotItem& b) {
                  return a.score > b.score;
              });

    // 只保留 top max_recall_ * 2 条，节省内存
    if ((int)new_list.size() > max_recall_ * 2) {
        new_list.resize(max_recall_ * 2);
    }

    {
        std::lock_guard<std::mutex> lock(hot_list_mutex_);
        hot_list_cache_ = std::move(new_list);
        new_size = hot_list_cache_.size();   // BUG-6 修复：在锁内读取 size
    }
    last_refresh_time_.store(now);
    LOG_INFO("HotContentRecallProcessor: hot list refreshed, size={}", new_size);
}

int HotContentRecallProcessor::Process(Session& session) {
    if (!enabled_) return 0;

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // BUG-6 修复：用 CAS 确保只有一个线程执行刷新，防止并发惊群
    int64_t old_time = last_refresh_time_.load();
    if (now - old_time > refresh_interval_sec_) {
        if (last_refresh_time_.compare_exchange_strong(old_time, now)) {
            RefreshHotList();  // 只有 CAS 成功的线程执行刷新
        }
    }

    // 构建已有 doc_id 集合，O(1) 去重
    std::unordered_set<std::string> existing_ids;
    for (const auto& cand : session.recall_results) {
        existing_ids.insert(cand.doc_id);
    }

    std::vector<HotItem> snapshot;
    {
        std::lock_guard<std::mutex> lock(hot_list_mutex_);
        snapshot = hot_list_cache_;
    }

    if (snapshot.empty()) {
        session.counts.recall_source_counts["hot_content"] = 0;
        return 0;
    }

    int count = 0;
    for (const auto& item : snapshot) {
        if (count >= max_recall_) break;
        if (existing_ids.count(item.doc_id)) continue;

        DocCandidate cand;
        cand.doc_id        = item.doc_id;
        cand.recall_source = "hot_content";
        cand.recall_score  = item.score;
        cand.click_count   = item.click_count;
        cand.like_count    = item.like_count;
        cand.publish_time  = item.publish_time;
        cand.title         = item.title;
        cand.author        = item.author;
        cand.quality_score = item.quality_score;
        session.recall_results.push_back(cand);
        existing_ids.insert(item.doc_id);
        count++;
    }

    session.counts.recall_source_counts["hot_content"] = count;
    LOG_INFO("HotContentRecallProcessor: recalled {} hot docs", count);
    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
REGISTER_MSR_PROCESSOR(HotContentRecallProcessor);
