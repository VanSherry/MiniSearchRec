// ============================================================
// MiniSearchRec - 查询归一化
// 参考：微信 QP（Query Processing）模块
// 作用：将用户输入的查询进行归一化，提升召回质量
// ============================================================

#ifndef MINISEARCHREC_QUERY_NORMALIZER_H
#define MINISEARCHREC_QUERY_NORMALIZER_H

#include <string>
#include <vector>
#include "../core/session.h"

namespace minisearchrec {

// ============================================================
// 查询归一化处理器
// 对原始 Query 进行清洗、归一化
// ============================================================
class QueryNormalizer {
public:
    QueryNormalizer() = default;
    ~QueryNormalizer() = default;

    // 归一化主函数
    // 输入：原始查询
    // 输出：归一化后的查询
    std::string Normalize(const std::string& raw_query) const;

    // 完整处理（更新 Session 中的 qp_info）
    void Process(Session& session) const;

    // 具体归一化方法
    // 1. 转小写（英文部分）
    static std::string ToLower(const std::string& query);

    // 2. 繁体转简体（简化版本，实际需要词典）
    static std::string TraditionalToSimplified(const std::string& query);

    // 3. 全角转半角
    static std::string FullWidthToHalfWidth(const std::string& query);

    // 4. 去除无意义字符（emoji、特殊符号等）
    static std::string RemoveNoise(const std::string& query);

    // 5. 去除停用词（的、了、is、the 等）
    static std::string RemoveStopWords(const std::string& query);

    // 6. 拼写纠错（简化版本）
    static std::string SpellCorrection(const std::string& query);

    // 7. 英文字母数字归一化（iPhone14 -> iphone 14）
    static std::string NormalizeAlnum(const std::string& query);

    // 8. 连续空格合并
    static std::string CollapseSpaces(const std::string& query);

    // 判断是否需要进行归一化
    bool NeedsNormalization(const std::string& raw_query) const;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUERY_NORMALIZER_H
