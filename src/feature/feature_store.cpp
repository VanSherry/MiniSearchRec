// =========================================================
// MiniSearchRec - 特征存储实现
// 参考：微信 FeatureUnion、X(Twitter) FeatureStore
// =========================================================

#include "feature/feature_store.h"
#include <algorithm>

namespace minisearchrec {

void FeatureStore::SetQueryFeature(const std::string& key, float value) {
    query_features_[key] = value;
}

void FeatureStore::SetUserFeature(const std::string& key, float value) {
    user_features_[key] = value;
}

void FeatureStore::SetDocFeature(const std::string& doc_id,
                                  const std::string& key,
                                  float value) {
    doc_features_[doc_id][key] = value;
}

float FeatureStore::GetQueryFeature(const std::string& key,
                                    float default_val) const {
    auto it = query_features_.find(key);
    return (it != query_features_.end()) ? it->second : default_val;
}

float FeatureStore::GetUserFeature(const std::string& key,
                                    float default_val) const {
    auto it = user_features_.find(key);
    return (it != user_features_.end()) ? it->second : default_val;
}

float FeatureStore::GetDocFeature(const std::string& doc_id,
                                   const std::string& key,
                                   float default_val) const {
    auto it = doc_features_.find(doc_id);
    if (it == doc_features_.end()) return default_val;
    
    auto fit = it->second.find(key);
    return (fit != it->second.end()) ? fit->second : default_val;
}

std::vector<float> FeatureStore::GetDocFeatureVector(
    const std::string& doc_id,
    const std::vector<std::string>& feature_names
) const {
    std::vector<float> result;
    result.reserve(feature_names.size());
    
    for (const auto& name : feature_names) {
        result.push_back(GetDocFeature(doc_id, name));
    }
    return result;
}

void FeatureStore::Clear() {
    query_features_.clear();
    user_features_.clear();
    doc_features_.clear();
}

} // namespace minisearchrec
