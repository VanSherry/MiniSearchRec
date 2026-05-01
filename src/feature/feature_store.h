// ==========================================================
// MiniSearchRec - 特征存储
// 参考：微信 FeatureUnion、X(Twitter) FeatureStore
// ==========================================================

#ifndef MINISEARCHREC_FEATURE_STORE_H
#define MINISEARCHREC_FEATURE_STORE_H

#include <string>
#include <vector>
#include <unordered_map>

namespace minisearchrec {

// ==========================================================
// 特征存储
// 管理 Query、User、Doc 三类特征
// ==========================================================
class FeatureStore {
public:
    FeatureStore() = default;
    ~FeatureStore() = default;

    // 设置特征
    void SetQueryFeature(const std::string& key, float value);
    void SetUserFeature(const std::string& key, float value);
    void SetDocFeature(const std::string& doc_id,
                       const std::string& key,
                       float value);

    // 获取特征
    float GetQueryFeature(const std::string& key,
                           float default_val = 0.0f) const;
    float GetUserFeature(const std::string& key,
                           float default_val = 0.0f) const;
    float GetDocFeature(const std::string& doc_id,
                          const std::string& key,
                          float default_val = 0.0f) const;

    // 获取文档所有特征（用于排序模型）
    std::vector<float> GetDocFeatureVector(
        const std::string& doc_id,
        const std::vector<std::string>& feature_names
    ) const;

    // 清空
    void Clear();

private:
    std::unordered_map<std::string, float> query_features_;
    std::unordered_map<std::string, float> user_features_;
    std::unordered_map<std::string,
                     std::unordered_map<std::string, float>> doc_features_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_FEATURE_STORE_H
