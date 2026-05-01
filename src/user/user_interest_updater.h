// ============================================================
// MiniSearchRec - 用户兴趣更新器
// 参考：微信 UserInterestUpdater、X(Twitter) InterestModel
// 作用：根据用户行为，使用 EMA 增量更新用户兴趣模型
// EMA 公式：new_weight = α * signal + (1 - α) * old_weight
// ============================================================

#ifndef MINISEARCHREC_USER_INTEREST_UPDATER_H
#define MINISEARCHREC_USER_INTEREST_UPDATER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "../core/session.h"
#include "user_profile.h"

namespace minisearchrec {

// ============================================================
// 用户兴趣更新器
// 负责：根据点击、浏览、点赞等行为，使用 EMA 增量更新用户兴趣模型
// ============================================================
class UserInterestUpdater {
public:
    UserInterestUpdater() = default;
    ~UserInterestUpdater() = default;

    // 更新兴趣模型（主函数）：EMA 增量更新，不清空旧权重
    void UpdateInterests(UserProfile& profile) const;

    // 批量更新（用于离线任务）
    void BatchUpdate(const std::vector<std::string>& user_ids);

    // 具体更新方法
    // 1. 基于点击历史更新（EMA）
    void UpdateFromClickHistory(UserProfile& profile) const;

    // 2. 基于浏览历史更新
    void UpdateFromViewHistory(UserProfile& profile) const;

    // 3. 基于点赞历史更新（EMA，权重更高）
    void UpdateFromLikeHistory(UserProfile& profile) const;

    // 4. 基于搜索历史更新（EMA，弱信号）
    void UpdateFromSearchHistory(UserProfile& profile) const;

    // 5. 时间衰减：老的兴趣权重降低
    void ApplyTimeDecay(UserProfile& profile) const;

    // 6. 兴趣扩散：相关类别也增加权重
    void DiffuseInterests(UserProfile& profile) const;

    // 获取类别权重（从文档 ID 或内容推断）
    float GetCategoryWeight(const std::string& doc_id,
                            const std::string& category) const;

    // 设置更新参数
    void SetDecayFactor(float factor) { decay_factor_ = factor; }
    void SetClickWeight(float weight) { click_weight_ = weight; }
    void SetViewWeight(float weight) { view_weight_ = weight; }
    void SetLikeWeight(float weight) { like_weight_ = weight; }
    void SetEmaAlpha(float alpha) { ema_alpha_ = alpha; }

private:
    float decay_factor_ = 0.95f;   // 时间衰减因子
    float click_weight_ = 0.5f;    // 点击信号强度
    float view_weight_ = 0.2f;     // 浏览信号强度
    float like_weight_ = 0.8f;     // 点赞信号强度
    float ema_alpha_ = 0.3f;       // EMA 学习率（α），越大越偏向新信号

    // 类别关联关系（用于兴趣扩散）
    std::unordered_map<std::string, std::vector<std::string>> category_links_ = {
        {"technology", {"programming", "AI", "gadget"}},
        {"tech",       {"programming", "AI", "gadget"}},
        {"food",       {"cooking", "restaurant", "nutrition"}},
        {"travel",     {"photography", "outdoor", "culture"}},
    };
};

} // namespace minisearchrec

#endif // MINISEARCHREC_USER_INTEREST_UPDATER_H
