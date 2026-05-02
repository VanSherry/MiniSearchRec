// ==========================================================
// MiniSearchRec - 用户特征提取
// 参考：业界用户特征方案
// ==========================================================

#ifndef MINISEARCHREC_USER_FEATURES_H
#define MINISEARCHREC_USER_FEATURES_H

#include <string>
#include <vector>
#include <unordered_map>
#include "biz/search/search_session.h"
#include "framework/config/config_manager.h"

namespace minisearchrec {

// ==========================================================
// 用户特征提取器
// 从用户画像中提取各类特征，用于精排模型
// ==========================================================
class UserFeatures {
public:
    UserFeatures() = default;
    ~UserFeatures() = default;

    // 提取所有用户特征
    void Extract(const Session& session,
                 std::unordered_map<std::string, float>& features) const;

    // 具体特征提取方法
    // 用户活跃度（基于登录频率）
    static float UserActivity(const UserProfile& profile);

    // 用户兴趣集中度（熵：兴趣分布越集中，值越小）
    static float InterestConcentration(const UserProfile& profile);

    // 用户历史点击数
    static float HistoryClickCount(const UserProfile& profile);

    // 用户历史浏览数
    static float HistoryViewCount(const UserProfile& profile);

    // 用户留存天数
    static float RetentionDays(const UserProfile& profile);

    // 用户偏好类别匹配度（与当前查询类别）
    static float CategoryMatch(const UserProfile& profile,
                               const std::string& inferred_category);

    // 用户点击率（CTR）
    static float UserCTR(const UserProfile& profile);

    // 用户停留时间（平均）
    static float AvgDwellTime(const UserProfile& profile);

    // 是否新用户（注册 < 7天）
    static float IsNewUser(const UserProfile& profile);

    // 是否高价值用户（基于历史行为）
    static float IsHighValueUser(const UserProfile& profile);

    // 用户设备类型（0=未知，1=iOS，2=Android，3=Web）
    static float DeviceType(const UserProfile& profile);

    // 用户时段偏好（0=未知，1=早，2=中，3=晚）
    static float TimePreference(const UserProfile& profile);

    // 提取单个特征（按名称）
    float ExtractFeature(const std::string& feature_name,
                         const Session& session) const;

    // 获取特征名称列表
    std::vector<std::string> GetFeatureNames() const;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_USER_FEATURES_H
