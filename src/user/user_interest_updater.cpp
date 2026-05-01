// ============================================================
// MiniSearchRec - 用户兴趣更新器实现
// 使用真正的 EMA（指数移动平均）进行增量更新
// EMA 公式：new_weight = α * signal + (1 - α) * old_weight
// ============================================================

#include "user/user_interest_updater.h"
#include "core/app_context.h"
#include "utils/logger.h"
#include <algorithm>
#include <cmath>

namespace minisearchrec {

// 从 DocStore 获取文档类别
static std::string FetchDocCategory(const std::string& doc_id) {
    auto store = AppContext::Instance().GetDocStore();
    if (!store) return "";
    Document doc;
    if (!store->GetDoc(doc_id, doc)) return "";
    return doc.category();
}

void UserInterestUpdater::UpdateInterests(UserProfile& profile) const {
    // EMA 增量更新：不清空旧权重，而是在旧权重基础上做衰减+新信号融合
    ApplyTimeDecay(profile);
    UpdateFromClickHistory(profile);
    UpdateFromLikeHistory(profile);
    UpdateFromSearchHistory(profile);
    DiffuseInterests(profile);

    // 归一化：确保所有类别权重总和 ≤ 1
    float total = 0.0f;
    for (const auto& [cat, w] : profile.category_weights()) {
        total += w;
    }
    if (total > 1.0f) {
        auto& weights = *profile.mutable_category_weights();
        for (auto& [cat, w] : weights) {
            w /= total;
        }
    }

    LOG_DEBUG("UserInterestUpdater: uid={}, categories={}",
              profile.uid(), profile.category_weights_size());
}

void UserInterestUpdater::BatchUpdate(const std::vector<std::string>& user_ids) {
    UserProfileManager mgr;
    for (const auto& uid : user_ids) {
        UserProfile profile;
        if (mgr.LoadProfile(uid, profile)) {
            UpdateInterests(profile);
            mgr.SaveProfile(profile);
            LOG_DEBUG("BatchUpdate: updated uid={}", uid);
        }
    }
}

void UserInterestUpdater::UpdateFromClickHistory(UserProfile& profile) const {
    auto& weights = *profile.mutable_category_weights();
    for (const auto& doc_id : profile.click_doc_ids()) {
        std::string cat = FetchDocCategory(doc_id);
        if (!cat.empty()) {
            // EMA: new = α * signal + (1-α) * old
            float old_w = weights.count(cat) ? weights[cat] : 0.0f;
            weights[cat] = ema_alpha_ * click_weight_ + (1.0f - ema_alpha_) * old_w;
        }
    }
}

void UserInterestUpdater::UpdateFromViewHistory(UserProfile& /*profile*/) const {
    // proto UserProfile 中无 view_history 字段，跳过
}

void UserInterestUpdater::UpdateFromLikeHistory(UserProfile& profile) const {
    auto& weights = *profile.mutable_category_weights();
    for (const auto& doc_id : profile.like_doc_ids()) {
        std::string cat = FetchDocCategory(doc_id);
        if (!cat.empty()) {
            // 点赞权重更高，EMA 更新
            float old_w = weights.count(cat) ? weights[cat] : 0.0f;
            weights[cat] = ema_alpha_ * like_weight_ + (1.0f - ema_alpha_) * old_w;
        }
    }
}

void UserInterestUpdater::UpdateFromSearchHistory(UserProfile& profile) const {
    // 根据搜索词推断类别（简化：关键词匹配）
    static const std::unordered_map<std::string, std::string> keyword_cat = {
        {"技术", "tech"}, {"代码", "tech"}, {"编程", "tech"}, {"AI", "tech"},
        {"美食", "food"}, {"餐厅", "food"}, {"旅游", "travel"}, {"酒店", "travel"},
        {"健康", "health"}, {"运动", "sports"},
    };

    auto& weights = *profile.mutable_category_weights();
    for (const auto& query : profile.query_history()) {
        for (const auto& [kw, cat] : keyword_cat) {
            if (query.find(kw) != std::string::npos) {
                // 搜索信号较弱，alpha 减半
                float old_w = weights.count(cat) ? weights[cat] : 0.0f;
                float search_signal = 0.05f;
                weights[cat] = (ema_alpha_ * 0.5f) * search_signal
                             + (1.0f - ema_alpha_ * 0.5f) * old_w;
            }
        }
    }
}

void UserInterestUpdater::ApplyTimeDecay(UserProfile& profile) const {
    // 利用 active_days_last_30 估算衰减天数
    int inactive_days = 30 - std::min(30, profile.active_days_last_30());
    float decay = std::pow(decay_factor_,
                           static_cast<float>(inactive_days));

    auto& weights = *profile.mutable_category_weights();
    for (auto& [cat, w] : weights) {
        w *= decay;
    }
}

void UserInterestUpdater::DiffuseInterests(UserProfile& profile) const {
    // 兴趣扩散：相关类别获得 30% 权重
    auto& weights = *profile.mutable_category_weights();
    std::unordered_map<std::string, float> additions;

    for (const auto& [cat, w] : weights) {
        auto it = category_links_.find(cat);
        if (it != category_links_.end()) {
            for (const auto& linked : it->second) {
                additions[linked] += w * 0.3f;
            }
        }
    }

    for (const auto& [cat, delta] : additions) {
        weights[cat] = std::min(1.0f, weights[cat] + delta);
    }
}

float UserInterestUpdater::GetCategoryWeight(const std::string& doc_id,
                                              const std::string& category) const {
    std::string cat = FetchDocCategory(doc_id);
    return (cat == category) ? 1.0f : 0.0f;
}

} // namespace minisearchrec
