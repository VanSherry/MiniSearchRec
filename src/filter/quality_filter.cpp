// ============================================================
// MiniSearchRec - 质量过滤器实现
// ============================================================

#include "filter/quality_filter.h"

namespace minisearchrec {

bool QualityFilterProcessor::Init(const YAML::Node& config) {
    if (config["min_quality_score"]) {
        min_quality_score_ = config["min_quality_score"].as<float>(0.3f);
    }
    if (config["min_click_count"]) {
        min_click_count_ = config["min_click_count"].as<int32_t>(0);
    }
    if (config["min_content_length"]) {
        min_content_length_ = config["min_content_length"].as<int32_t>(50);
    }
    return true;
}

bool QualityFilterProcessor::ShouldKeep(const Session& session,
                                          const DocCandidate& candidate) {
    // 质量分过滤
    if (candidate.quality_score < min_quality_score_) {
        return false;
    }
    // 点击数过滤
    if (candidate.click_count < min_click_count_) {
        return false;
    }
    // 内容长度过滤（content_snippet 长度作为近似）
    if (static_cast<int32_t>(candidate.content_snippet.size()) < min_content_length_) {
        return false;
    }
    return true;
}

} // namespace minisearchrec
