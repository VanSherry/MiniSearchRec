// ============================================================
// MiniSearchRec - 用户兴趣更新器实现
// 根据用户历史行为定期重新计算兴趣权重
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
    // 清空现有分类权重，从历史重新计算
    profile.mutable_category_weights()->clear();

    UpdateFromClickHistory(profile);
    UpdateFromLikeHistory(profile);
    UpdateFromSearchHistory(profile);
    ApplyTimeDecay(profile);
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
            weights[cat] = std::min(1.0f, weights[cat] + click_weight_);
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
            // 点赞权重更高
            weights[cat] = std::min(1.0f, weights[cat] + like_weight_);
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
                weights[cat] = std::min(1.0f, weights[cat] + 0.05f);
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
