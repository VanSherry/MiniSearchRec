// ============================================================
// MiniSearchRec - 时效性（新鲜度）打分器实现
// 新发布的内容获得更高分数
// ============================================================

#include "rank/freshness_scorer.h"
#include <cmath>
#include <chrono>

namespace minisearchrec {

bool FreshnessScorerProcessor::Init(const YAML::Node& config) {
    if (config["weight"]) {
        weight_ = config["weight"].as<float>(1.0f);
    }
    if (config["max_age_days"]) {
        max_age_days_ = config["max_age_days"].as<int>(365);
    }
    if (config["decay_rate"]) {
        decay_rate_ = config["decay_rate"].as<float>(0.01f);
    }
    return true;
}

int FreshnessScorerProcessor::Process(Session& session,
                                       std::vector<DocCandidate>& candidates) {
    if (candidates.empty()) return 0;

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    for (auto& cand : candidates) {
        int64_t age_seconds = now - cand.publish_time;
        int64_t age_days = age_seconds / 86400;

        float freshness_score = 0.0f;
        if (age_days <= 1) {
            freshness_score = 1.0f;  // 24小时内的内容满分
        } else if (age_days <= 7) {
            freshness_score = 0.8f;  // 一周内
        } else if (age_days <= 30) {
            freshness_score = 0.5f;  // 一个月内
        } else if (age_days <= max_age_days_) {
            freshness_score = 0.3f;  // 一年内
        }
        // 超过 max_age_days_ 为 0

        cand.coarse_score += freshness_score * weight_;
        cand.debug_scores["freshness"] = freshness_score;
    }

    return 0;
}

} // namespace minisearchrec
