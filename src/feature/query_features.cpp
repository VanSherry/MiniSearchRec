// ==========================================================
// MiniSearchRec - 查询特征提取实现
// ==========================================================

#include "feature/query_features.h"
#include "utils/string_utils.h"
#include <unordered_set>

namespace minisearchrec {

void QueryFeatures::Extract(const Session& session,
                             std::unordered_map<std::string, float>& features) const {
    const auto& qp_info = session.qp_info;
    const std::string& raw_query = qp_info.raw_query;
    const std::vector<std::string>& terms = qp_info.terms;

    features["query_length"] = QueryLength(raw_query);
    features["term_count"] = TermCount(terms);
    features["avg_term_length"] = AvgTermLength(terms);
    features["has_chinese"] = HasChinese(raw_query);
    features["has_english"] = HasEnglish(raw_query);
    features["has_number"] = HasNumber(raw_query);
    features["has_special_char"] = HasSpecialChar(raw_query);
    features["query_type"] = QueryType(raw_query);
    features["language"] = LanguageDetect(raw_query);
    features["query_complexity"] = QueryComplexity(terms);
    features["is_hot_query"] = IsHotQuery(raw_query);
    features["time_sensitivity"] = TimeSensitivity(raw_query);
    features["expected_result_count"] = ExpectedResultCount(raw_query);
}

float QueryFeatures::QueryLength(const std::string& query) {
    return static_cast<float>(query.length());
}

float QueryFeatures::TermCount(const std::vector<std::string>& terms) {
    return static_cast<float>(terms.size());
}

float QueryFeatures::AvgTermLength(const std::vector<std::string>& terms) {
    if (terms.empty()) return 0.0f;
    
    float total_len = 0.0f;
    for (const auto& term : terms) {
        total_len += static_cast<float>(term.length());
    }
    return total_len / terms.size();
}

float QueryFeatures::HasChinese(const std::string& query) {
    return utils::ContainsChinese(query) ? 1.0f : 0.0f;
}

float QueryFeatures::HasEnglish(const std::string& query) {
    for (char c : query) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            return 1.0f;
        }
    }
    return 0.0f;
}

float QueryFeatures::HasNumber(const std::string& query) {
    for (char c : query) {
        if (c >= '0' && c <= '9') {
            return 1.0f;
        }
    }
    return 0.0f;
}

float QueryFeatures::HasSpecialChar(const std::string& query) {
    for (char c : query) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ') {
            return 1.0f;
        }
    }
    return 0.0f;
}

float QueryFeatures::QueryType(const std::string& query) {
    std::string lower_query = utils::ToLower(query);
    
    // 导航型：包含"官网"、"登录"、"下载"等
    if (lower_query.find("官网") != std::string::npos ||
        lower_query.find("登录") != std::string::npos ||
        lower_query.find("download") != std::string::npos) {
        return 1.0f;
    }
    
    // 事务型：包含"购买"、"订单"、"价格"等
    if (lower_query.find("购买") != std::string::npos ||
        lower_query.find("订单") != std::string::npos ||
        lower_query.find("价格") != std::string::npos ||
        lower_query.find("buy") != std::string::npos) {
        return 3.0f;
    }
    
    // 信息型：包含"怎么"、"什么"、"如何"等
    if (lower_query.find("怎么") != std::string::npos ||
        lower_query.find("什么") != std::string::npos ||
        lower_query.find("如何") != std::string::npos ||
        lower_query.find("how") != std::string::npos ||
        lower_query.find("what") != std::string::npos) {
        return 2.0f;
    }
    
    return 2.0f;  // 默认为信息型
}

float QueryFeatures::LanguageDetect(const std::string& query) {
    bool has_chinese = HasChinese(query) > 0.5f;
    bool has_english = HasEnglish(query) > 0.5f;
    
    if (has_chinese && has_english) return 3.0f;  // 混合
    if (has_chinese) return 1.0f;                 // 中文
    if (has_english) return 2.0f;                 // 英文
    return 0.0f;                                  // 未知
}

float QueryFeatures::QueryComplexity(const std::vector<std::string>& terms) {
    if (terms.empty()) return 0.0f;
    
    // 基于独特词数 / 总词数
    std::unordered_set<std::string> unique_terms(terms.begin(), terms.end());
    float uniqueness = static_cast<float>(unique_terms.size()) / terms.size();
    
    // 基于词数（越多越复杂）
    float term_factor = std::min(static_cast<float>(terms.size()) / 10.0f, 1.0f);
    
    return (uniqueness + term_factor) / 2.0f;
}

float QueryFeatures::IsHotQuery(const std::string& query) {
    // 简化实现：基于查询长度和内容启发式判断
    // 实际项目中会从日志统计中获取热门查询列表
    if (query.length() < 2) return 1.0f;  // 短查询通常是热门的
    
    std::string lower_query = utils::ToLower(query);
    // 常见热门词
    static const std::vector<std::string> hot_words = {
        "新闻", "热点", "直播", "视频", "音乐", "游戏", 
        "news", "video", "music", "game"
    };
    
    for (const auto& word : hot_words) {
        if (lower_query.find(word) != std::string::npos) {
            return 1.0f;
        }
    }
    
    return 0.0f;
}

float QueryFeatures::TimeSensitivity(const std::string& query) {
    std::string lower_query = utils::ToLower(query);
    
    // 时间敏感关键词
    static const std::vector<std::string> time_keywords = {
        "最新", "今天", "昨天", "本周", "本月", "最近", "实时",
        "latest", "today", "yesterday", "this week", "recent", "live"
    };
    
    for (const auto& keyword : time_keywords) {
        if (lower_query.find(keyword) != std::string::npos) {
            return 1.0f;
        }
    }
    
    return 0.0f;
}

float QueryFeatures::ExpectedResultCount(const std::string& query) {
    // 基于查询类型推测预期结果数
    float query_type = QueryType(query);
    
    // 导航型查询预期结果少（精准）
    if (query_type == 1.0f) return 0.2f;
    
    // 事务型查询预期结果中等
    if (query_type == 3.0f) return 0.5f;
    
    // 信息型查询预期结果多
    return 1.0f;
}

float QueryFeatures::ExtractFeature(const std::string& feature_name,
                                    const Session& session) const {
    const auto& qp_info = session.qp_info;
    const std::string& raw_query = qp_info.raw_query;
    const std::vector<std::string>& terms = qp_info.terms;

    if (feature_name == "query_length") return QueryLength(raw_query);
    if (feature_name == "term_count") return TermCount(terms);
    if (feature_name == "avg_term_length") return AvgTermLength(terms);
    if (feature_name == "has_chinese") return HasChinese(raw_query);
    if (feature_name == "has_english") return HasEnglish(raw_query);
    if (feature_name == "has_number") return HasNumber(raw_query);
    if (feature_name == "query_type") return QueryType(raw_query);
    if (feature_name == "language") return LanguageDetect(raw_query);
    if (feature_name == "query_complexity") return QueryComplexity(terms);
    if (feature_name == "time_sensitivity") return TimeSensitivity(raw_query);
    
    return 0.0f;
}

std::vector<std::string> QueryFeatures::GetFeatureNames() const {
    return {
        "query_length",
        "term_count",
        "avg_term_length",
        "has_chinese",
        "has_english",
        "has_number",
        "has_special_char",
        "query_type",
        "language",
        "query_complexity",
        "is_hot_query",
        "time_sensitivity",
        "expected_result_count"
    };
}

} // namespace minisearchrec
