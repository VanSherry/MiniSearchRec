// ==========================================================
// MiniSearchRec - 用户画像管理器实现
// 使用 SQLite 持久化用户画像（通过 proto 序列化）
// ==========================================================

#include "lib/user/user_profile.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>

namespace minisearchrec {

// 用文件系统序列化 proto（生产环境应用 Redis/DB）
static std::string ProfilePath(const std::string& uid) {
    return "./data/user_profiles/" + uid + ".pb";
}

bool UserProfileManager::LoadProfile(const std::string& uid, UserProfile& profile) {
    if (uid.empty()) return false;

    std::string path = ProfilePath(uid);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        // 新用户，返回空画像
        profile.set_uid(uid);
        profile.set_update_time(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        return true;
    }

    std::string data((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
    if (!profile.ParseFromString(data)) {
        LOG_WARN("UserProfileManager: Failed to parse profile for uid={}", uid);
        profile.set_uid(uid);
        return false;
    }

    LOG_DEBUG("UserProfileManager: Loaded profile for uid={}, clicks={}",
              uid, profile.total_clicks());
    return true;
}

bool UserProfileManager::SaveProfile(const UserProfile& profile) {
    std::string path = ProfilePath(profile.uid());

    // 确保目录存在
    try {
        std::filesystem::create_directories("./data/user_profiles");
    } catch (...) {}

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        LOG_ERROR("UserProfileManager: Cannot open {} for writing", path);
        return false;
    }

    std::string data;
    if (!profile.SerializeToString(&data)) {
        LOG_ERROR("UserProfileManager: Serialization failed for uid={}", profile.uid());
        return false;
    }

    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

bool UserProfileManager::UpdateFromEvent(const std::string& uid,
                                          const std::string& doc_id,
                                          const std::string& event_type) {
    UserProfile profile;
    LoadProfile(uid, profile);

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    profile.set_update_time(now);

    // 根据事件类型更新画像
    if (event_type == "click") {
        // 添加到点击历史（保留最近 200 条）
        if (profile.click_doc_ids_size() < 200) {
            profile.add_click_doc_ids(doc_id);
        } else {
            // 移除最旧的，添加最新的（proto repeated 不支持直接 pop，重建）
            auto* ids = profile.mutable_click_doc_ids();
            ids->erase(ids->begin());
            ids->Add(std::string(doc_id));
        }
        profile.set_total_clicks(profile.total_clicks() + 1);
    } else if (event_type == "like") {
        if (profile.like_doc_ids_size() < 200) {
            profile.add_like_doc_ids(doc_id);
        }
        profile.set_total_likes(profile.total_likes() + 1);
    }

    // 从 DocStore 获取文档类别，更新 category_weights
    auto doc_store = AppContext::Instance().GetDocStore();
    if (doc_store) {
        Document doc;
        if (doc_store->GetDoc(doc_id, doc) && !doc.category().empty()) {
            float weight = 0.1f;
            if (event_type == "like")    weight = 0.4f;
            if (event_type == "collect") weight = 0.6f;

            auto& cat_weights = *profile.mutable_category_weights();
            cat_weights[doc.category()] =
                std::min(1.0f, cat_weights[doc.category()] + weight);
        }
    }

    return SaveProfile(profile);
}

std::vector<float> UserProfileManager::GetInterestEmbedding(const std::string& uid) {
    UserProfile profile;
    if (!LoadProfile(uid, profile)) return {};

    return std::vector<float>(
        profile.interest_embedding().begin(),
        profile.interest_embedding().end());
}

bool UserProfileManager::UpdateInterestEmbedding(const std::string& uid,
                                                   const std::vector<float>& doc_embedding,
                                                   float weight) {
    if (doc_embedding.empty()) return false;

    UserProfile profile;
    LoadProfile(uid, profile);

    auto* emb = profile.mutable_interest_embedding();
    if (emb->empty()) {
        // 初始化：直接赋值
        for (float v : doc_embedding) emb->Add(v);
    } else if (emb->size() == (int)doc_embedding.size()) {
        // EMA 更新：new = (1-w)*old + w*new
        for (int i = 0; i < emb->size(); ++i) {
            (*emb)[i] = (1.0f - weight) * (*emb)[i] + weight * doc_embedding[i];
        }
    }

    return SaveProfile(profile);
}

} // namespace minisearchrec
