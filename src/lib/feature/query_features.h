// ==========================================================
// MiniSearchRec - 查询特征提取
// 参考：业界查询理解方案
// ==========================================================

#ifndef MINISEARCHREC_QUERY_FEATURES_H
#define MINISEARCHREC_QUERY_FEATURES_H

#include <string>
#include <vector>
#include <unordered_map>
#include "biz/search/search_session.h"

namespace minisearchrec {

// ==========================================================
// 查询特征提取器
// 从 Query 中提取各类特征，用于精排模型
// ==========================================================
class QueryFeatures {
public:
    QueryFeatures() = default;
    ~QueryFeatures() = default;

    // 提取所有查询特征
    void Extract(const Session& session,
                 std::unordered_map<std::string, float>& features) const;

    // 具体特征提取方法
    // 查询长度（字符数）
    static float QueryLength(const std::string& query);

    // 查询词数（term count）
    static float TermCount(const std::vector<std::string>& terms);

    // 平均词长度
    static float AvgTermLength(const std::vector<std::string>& terms);

    // 是否包含中文
    static float HasChinese(const std::string& query);

    // 是否包含英文
    static float HasEnglish(const std::string& query);

    // 是否包含数字
    static float HasNumber(const std::string& query);

    // 是否包含特殊字符
    static float HasSpecialChar(const std::string& query);

    // 查询类型（0=未知，1=导航型，2=信息型，3=事务型）
    static float QueryType(const std::string& query);

    // 语言检测（0=未知，1=中文，2=英文，3=混合）
    static float LanguageDetect(const std::string& query);

    // 查询复杂度（基于词数和独特词数）
    static float QueryComplexity(const std::vector<std::string>& terms);

    // 是否热门查询（需要外部数据，这里使用启发式）
    static float IsHotQuery(const std::string& query);

    // 时间敏感性（0=不敏感，1=敏感，如"最新"、"今天"等）
    static float TimeSensitivity(const std::string& query);

    // 查询结果期望值（预期返回结果数，启发式）
    static float ExpectedResultCount(const std::string& query);

    // 提取单个特征（按名称）
    float ExtractFeature(const std::string& feature_name,
                         const Session& session) const;

    // 获取特征名称列表
    std::vector<std::string> GetFeatureNames() const;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUERY_FEATURES_H
