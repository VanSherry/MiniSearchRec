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
        float age_days = static_cast<float>(age_seconds) / 86400.0f;

        float freshness_score = 0.0f;
        if (age_days >= 0 && age_days <= static_cast<float>(max_age_days_)) {
            // 指数衰减：score = exp(-decay_rate * age_days)
            // decay_rate=0.01 时：第1天≈0.99, 第7天≈0.93, 第30天≈0.74, 第365天≈0.026
            freshness_score = std::exp(-decay_rate_ * age_days);
        }
        // 超过 max_age_days_ 或发布时间为0 → 0分

        cand.coarse_score += freshness_score * weight_;
        cand.debug_scores["freshness"] = freshness_score;
    }

    return 0;
}

} // namespace minisearchrec
