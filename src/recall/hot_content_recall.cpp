// ============================================================
// MiniSearchRec - 热门内容召回处理器实现
// 推荐近期热门（高点击/高点赞）内容
// ============================================================

#include "recall/hot_content_recall.h"
#include "core/app_context.h"
#include "utils/logger.h"
#include <chrono>
#include <algorithm>
#include <cmath>

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
    return true;
}

int HotContentRecallProcessor::Process(Session& session) {
    if (!enabled_) return 0;

    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) {
        LOG_WARN("HotContentRecallProcessor: DocStore not available, skipping");
        return 0;
    }

    // 获取所有文档 ID
    auto all_ids = doc_store->GetAllDocIds();
    if (all_ids.empty()) {
        session.counts.recall_source_counts["hot_content"] = 0;
        return 0;
    }

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t time_window_sec = static_cast<int64_t>(time_window_hours_) * 3600;

    // 构建候选：加载文档元信息，计算热门分
    struct HotItem {
        std::string doc_id;
        float score;
        int64_t click_count;
        int64_t like_count;
        int64_t publish_time;
        std::string title;
        std::string author;
        float quality_score;
    };
    std::vector<HotItem> hot_items;
    hot_items.reserve(std::min((int)all_ids.size(), max_recall_ * 5));

    for (const auto& doc_id : all_ids) {
        Document doc;
        if (!doc_store->GetDoc(doc_id, doc)) continue;

        // 过滤时间窗口
        int64_t age = now - doc.publish_time();
        if (time_window_hours_ > 0 && age > time_window_sec) continue;

        // 热门分 = log(click+1) * 0.6 + log(like+1) * 0.4
        float hot_score = std::log1pf(static_cast<float>(doc.click_count())) * 0.6f
                        + std::log1pf(static_cast<float>(doc.like_count()))  * 0.4f;
        hot_items.push_back({doc_id, hot_score,
                              doc.click_count(), doc.like_count(),
                              doc.publish_time(), doc.title(),
                              doc.author(), doc.quality_score()});
    }

    // 按热门分降序排序
    std::sort(hot_items.begin(), hot_items.end(),
              [](const HotItem& a, const HotItem& b) {
                  return a.score > b.score;
              });

    int count = 0;
    for (const auto& item : hot_items) {
        if (count >= max_recall_) break;

        // 去重：检查是否已在 recall_results
        bool exists = false;
        for (const auto& cand : session.recall_results) {
            if (cand.doc_id == item.doc_id) { exists = true; break; }
        }
        if (exists) continue;

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
        count++;
    }

    session.counts.recall_source_counts["hot_content"] = count;
    LOG_INFO("HotContentRecallProcessor: recalled {} hot docs", count);
    return 0;
}

} // namespace minisearchrec
