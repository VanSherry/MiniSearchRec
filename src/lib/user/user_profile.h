// ==========================================================
// MiniSearchRec - 用户画像管理
// 参考：X(Twitter) UUA (Unified User Actions)
// ==========================================================

#ifndef MINISEARCHREC_USER_PROFILE_H
#define MINISEARCHREC_USER_PROFILE_H

#include <string>
#include <vector>
#include "user.pb.h"

namespace minisearchrec {

// ==========================================================
// 用户画像管理器
// ==========================================================
class UserProfileManager {
public:
    UserProfileManager() = default;
    ~UserProfileManager() = default;

    // 加载用户画像
    bool LoadProfile(const std::string& uid, UserProfile& profile);

    // 保存用户画像
    bool SaveProfile(const UserProfile& profile);

    // 更新用户行为（点击/点赞）
    bool UpdateFromEvent(const std::string& uid,
                        const std::string& doc_id,
                        const std::string& event_type);

    // 获取用户兴趣向量
    std::vector<float> GetInterestEmbedding(const std::string& uid);

    // 更新用户兴趣向量（EMA 方式）
    bool UpdateInterestEmbedding(const std::string& uid,
                               const std::vector<float>& doc_embedding,
                               float weight = 0.5f);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_USER_PROFILE_H
