// ============================================================
// MiniSearchRec - 质量分打分器实现
// ============================================================

#include "lib/rank/scorer/quality_scorer.h"
#include <algorithm>
#include <cmath>

namespace minisearchrec {

bool QualityScorerProcessor::Init(const YAML::Node& config) {
    if (config["weight"]) {
        weight_ = config["weight"].as<float>(1.0f);
    }
    if (config["click_weight"]) {
        click_weight_ = config["click_weight"].as<float>(0.3f);
    }
    if (config["like_weight"]) {
        like_weight_ = config["like_weight"].as<float>(0.4f);
    }
    if (config["quality_weight"]) {
        quality_weight_ = config["quality_weight"].as<float>(0.3f);
    }
    return true;
}

int QualityScorerProcessor::Process(Session& session,
                                    std::vector<DocCandidate>& candidates) {
    if (candidates.empty()) return 0;

    for (auto& cand : candidates) {
        // 归一化点击数（log 平滑）
        float click_score = std::log1pf(static_cast<float>(cand.click_count));
        click_score = std::tanh(click_score / 5.0f);  // 归一化到 [0, 1]

        // 归一化点赞数
        float like_score = std::log1pf(static_cast<float>(cand.like_count));
        like_score = std::tanh(like_score / 5.0f);

        // 质量分（已有，0-1）
        float quality_score = std::min(1.0f, std::max(0.0f, cand.quality_score));

        // 加权融合
        float final_score = click_weight_ * click_score
                         + like_weight_ * like_score
                         + quality_weight_ * quality_score;

        cand.coarse_score += final_score * weight_;
        cand.debug_scores["quality"] = final_score;
    }

    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
REGISTER_MSR_PROCESSOR(QualityScorerProcessor);
