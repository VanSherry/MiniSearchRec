// ==========================================================
// MiniSearchRec - 文档特征提取实现
// ==========================================================

#include "lib/feature/doc_features.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace minisearchrec {

void DocFeatures::Extract(const DocCandidate& doc,
                           const Session& session,
                           std::unordered_map<std::string, float>& features) const {
    int64_t now_ms = session.begin_time_us > 0 ? session.begin_time_us / 1000
                   : std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
    const auto& category = session.qp_info.inferred_category;
    const auto& terms = session.qp_info.terms;
    
    features["quality_score"] = QualityScore(doc);
    features["freshness"] = Freshness(doc, now_ms);
    features["click_count"] = ClickCount(doc);
    features["like_count"] = LikeCount(doc);
    features["title_length"] = TitleLength(doc);
    features["category_match"] = CategoryMatch(doc, category);
    features["keyword_coverage"] = KeywordCoverage(doc, terms);
    features["doc_popularity"] = DocPopularity(doc);
    features["authority"] = Authority(doc);
    features["readability"] = Readability(doc);
}

void DocFeatures::BatchExtract(const std::vector<DocCandidate>& docs,
                                const Session& session,
                                std::vector<std::unordered_map<std::string, float>>& features) const {
    features.resize(docs.size());
    for (size_t i = 0; i < docs.size(); ++i) {
        Extract(docs[i], session, features[i]);
    }
}

float DocFeatures::QualityScore(const DocCandidate& doc) {
    return doc.quality_score;
}

float DocFeatures::Freshness(const DocCandidate& doc, int64_t now_ms) {
    if (doc.publish_time <= 0) return 0.5f;  // 未知时间，返回中性值
    
    int64_t age_ms = now_ms - doc.publish_time;
    float age_days = age_ms / (1000.0f * 60 * 60 * 24);
    
    // 越新越好：0-7天=1.0，7-30天=0.5，>30天=0.1
    if (age_days <= 7.0f) return 1.0f;
    if (age_days <= 30.0f) return 0.5f;
    if (age_days <= 365.0f) return 0.2f;
    return 0.1f;
}

float DocFeatures::ClickCount(const DocCandidate& doc) {
    // 返回归一化的点击数（取log）
    if (doc.click_count <= 0) return 0.0f;
    return std::log1pf(static_cast<float>(doc.click_count));
}

float DocFeatures::LikeCount(const DocCandidate& doc) {
    // 返回归一化的点赞数（取log）
    if (doc.like_count <= 0) return 0.0f;
    return std::log1pf(static_cast<float>(doc.like_count));
}

float DocFeatures::TitleLength(const DocCandidate& doc) {
    return static_cast<float>(doc.title.length());
}

float DocFeatures::ContentLength(const std::string& doc_id,
                                 const DocStore& doc_store) {
    Document doc;
    if (!const_cast<DocStore&>(doc_store).GetDoc(doc_id, doc)) return 0.0f;
    return static_cast<float>(doc.content().length());
}

float DocFeatures::CategoryMatch(const DocCandidate& doc,
                                 const std::string& inferred_category) {
    if (inferred_category.empty() || doc.category.empty()) {
        return 0.5f;  // 未知，返回中性值
    }
    
    // 完全匹配
    if (doc.category == inferred_category) return 1.0f;
    
    // 部分匹配（包含关系）
    if (doc.category.find(inferred_category) != std::string::npos ||
        inferred_category.find(doc.category) != std::string::npos) {
        return 0.7f;
    }
    
    return 0.0f;  // 不匹配
}

float DocFeatures::KeywordCoverage(const DocCandidate& doc,
                                    const std::vector<std::string>& terms) {
    if (terms.empty()) return 0.0f;
    
    // 简单计算：标题+内容摘要中包含的查询词比例
    std::string text = doc.title + " " + doc.content_snippet;
    std::string text_lower = "";
    for (char c : text) {
        text_lower += std::tolower(static_cast<unsigned char>(c));
    }
    
    int matched = 0;
    for (const auto& term : terms) {
        std::string term_lower = "";
        for (char c : term) {
            term_lower += std::tolower(static_cast<unsigned char>(c));
        }
        if (text_lower.find(term_lower) != std::string::npos) {
            matched++;
        }
    }
    
    return static_cast<float>(matched) / terms.size();
}

float DocFeatures::DocPopularity(const DocCandidate& doc) {
    // 基于点击和点赞计算热度
    float click_factor = std::log1pf(static_cast<float>(doc.click_count));
    float like_factor = std::log1pf(static_cast<float>(doc.like_count));
    
    // 归一化到 [0, 1]
    float popularity = (click_factor + like_factor) / 20.0f;
    return std::min(popularity, 1.0f);
}

float DocFeatures::Authority(const DocCandidate& doc) {
    // 简化实现：基于作者名称（实际项目中会有作者权威性数据库）
    if (doc.author.empty()) return 0.3f;
    
    // 假设作者名称长度可以作为简单启发式（通常官方作者名称较规范）
    if (doc.author.length() > 10) return 0.8f;
    if (doc.author.length() > 5) return 0.5f;
    return 0.3f;
}

float DocFeatures::Readability(const DocCandidate& doc) {
    // 简化实现：基于内容长度和标题长度比
    if (doc.content_snippet.empty()) return 0.5f;
    
    float title_len = static_cast<float>(doc.title.length());
    float content_len = static_cast<float>(doc.content_snippet.length());
    
    if (content_len < 1.0f) return 0.5f;
    
    // 标题/内容比例（适中最好）
    float ratio = title_len / content_len;
    if (ratio > 0.1f && ratio < 0.3f) return 1.0f;  // 比例适中
    if (ratio > 0.05f && ratio < 0.5f) return 0.7f;  // 比例尚可
    return 0.4f;  // 比例不佳
}

float DocFeatures::ExtractFeature(const std::string& feature_name,
                                  const DocCandidate& doc,
                                  const Session& session) const {
    int64_t now_ms = session.begin_time_us > 0 ? session.begin_time_us / 1000
                   : std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
    const auto& category = session.qp_info.inferred_category;
    const auto& terms = session.qp_info.terms;
    
    if (feature_name == "quality_score") return QualityScore(doc);
    if (feature_name == "freshness") return Freshness(doc, now_ms);
    if (feature_name == "click_count") return ClickCount(doc);
    if (feature_name == "like_count") return LikeCount(doc);
    if (feature_name == "title_length") return TitleLength(doc);
    if (feature_name == "category_match") return CategoryMatch(doc, category);
    if (feature_name == "keyword_coverage") return KeywordCoverage(doc, terms);
    if (feature_name == "doc_popularity") return DocPopularity(doc);
    if (feature_name == "authority") return Authority(doc);
    if (feature_name == "readability") return Readability(doc);
    
    return 0.0f;
}

std::vector<std::string> DocFeatures::GetFeatureNames() const {
    return {
        "quality_score",
        "freshness",
        "click_count",
        "like_count",
        "title_length",
        "category_match",
        "keyword_coverage",
        "doc_popularity",
        "authority",
        "readability"
    };
}

} // namespace minisearchrec
