// ==========================================================
// MiniSearchRec - 用户特征提取实现
// 参考：微信 UserProfile、X(Twitter) UserFeatures
// ==========================================================

#include "feature/user_features.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace minisearchrec {

void UserFeatures::Extract(const Session& session,
                            std::unordered_map<std::string, float>& features) const {
    const UserProfile* profile = session.user_profile.get();
    if (!profile) {
        features["user_activity"]          = 0.0f;
        features["interest_concentration"] = 0.0f;
        features["history_click_count"]    = 0.0f;
        features["history_view_count"]     = 0.0f;
        features["retention_days"]         = 0.0f;
        features["user_ctr"]               = 0.5f;
        features["avg_dwell_time"]         = 0.5f;
        features["is_new_user"]            = 1.0f;
        features["is_high_value_user"]     = 0.0f;
        features["device_type"]            = 0.0f;
        features["time_preference"]        = 0.0f;
        features["category_match"]         = 0.5f;
        return;
    }

    features["user_activity"]          = UserActivity(*profile);
    features["interest_concentration"] = InterestConcentration(*profile);
    features["history_click_count"]    = HistoryClickCount(*profile);
    features["history_view_count"]     = HistoryViewCount(*profile);
    features["retention_days"]         = RetentionDays(*profile);
    features["user_ctr"]               = UserCTR(*profile);
    features["avg_dwell_time"]         = AvgDwellTime(*profile);
    features["is_new_user"]            = IsNewUser(*profile);
    features["is_high_value_user"]     = IsHighValueUser(*profile);
    features["device_type"]            = DeviceType(*profile);
    features["time_preference"]        = TimePreference(*profile);
    features["category_match"]         = CategoryMatch(*profile,
                                            session.qp_info.inferred_category);
}

float UserFeatures::UserActivity(const UserProfile& profile) {
    int active = profile.active_days_last_30();
    if (active >= 20) return 1.0f;
    if (active >= 10) return 0.7f;
    if (active >= 3)  return 0.4f;
    if (active >= 1)  return 0.2f;
    return 0.0f;
}

float UserFeatures::InterestConcentration(const UserProfile& profile) {
    const auto& weights = profile.category_weights();
    if (weights.empty()) return 0.0f;

    float entropy = 0.0f;
    for (const auto& [cat, w] : weights) {
        if (w > 1e-9f) entropy -= w * std::log(w);
    }
    float max_entropy = std::log(static_cast<float>(weights.size()));
    if (max_entropy < 1e-9f) return 1.0f;
    return 1.0f - (entropy / max_entropy);
}

float UserFeatures::HistoryClickCount(const UserProfile& profile) {
    return static_cast<float>(profile.total_clicks());
}

float UserFeatures::HistoryViewCount(const UserProfile& profile) {
    // 用 click_doc_ids 数量近似浏览量
    return static_cast<float>(profile.click_doc_ids_size());
}

float UserFeatures::RetentionDays(const UserProfile& profile) {
    return static_cast<float>(profile.active_days_last_30());
}

float UserFeatures::CategoryMatch(const UserProfile& profile,
                                   const std::string& inferred_category) {
    if (inferred_category.empty()) return 0.5f;
    const auto& weights = profile.category_weights();
    auto it = weights.find(inferred_category);
    if (it == weights.end()) return 0.0f;
    return std::min(1.0f, it->second);
}

float UserFeatures::UserCTR(const UserProfile& profile) {
    float avg_pos = profile.avg_click_position();
    if (avg_pos <= 0) return 0.5f;
    return std::max(0.0f, 1.0f - avg_pos / 20.0f);
}

float UserFeatures::AvgDwellTime(const UserProfile& /*profile*/) {
    return 0.5f;  // proto 暂无此字段，返回默认值
}

float UserFeatures::IsNewUser(const UserProfile& profile) {
    return profile.active_days_last_30() <= 3 ? 1.0f : 0.0f;
}

float UserFeatures::IsHighValueUser(const UserProfile& profile) {
    return (profile.total_clicks() > 100 && profile.total_likes() > 20)
           ? 1.0f : 0.0f;
}

float UserFeatures::DeviceType(const UserProfile& /*profile*/) {
    return 0.0f;
}

float UserFeatures::TimePreference(const UserProfile& /*profile*/) {
    return 0.0f;
}

float UserFeatures::ExtractFeature(const std::string& feature_name,
                                    const Session& session) const {
    std::unordered_map<std::string, float> features;
    Extract(session, features);
    auto it = features.find(feature_name);
    return it != features.end() ? it->second : 0.0f;
}

std::vector<std::string> UserFeatures::GetFeatureNames() const {
    return {
        "user_activity", "interest_concentration",
        "history_click_count", "history_view_count",
        "retention_days", "user_ctr", "avg_dwell_time",
        "is_new_user", "is_high_value_user",
        "device_type", "time_preference", "category_match"
    };
}

} // namespace minisearchrec
