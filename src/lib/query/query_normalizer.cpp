// ============================================================
// MiniSearchRec - 查询归一化实现
// ============================================================

#include "lib/query/query_normalizer.h"
#include "utils/string_utils.h"
#include <unordered_set>

namespace minisearchrec {

std::string QueryNormalizer::Normalize(const std::string& raw_query) const {
    if (raw_query.empty()) return "";
    
    std::string result = raw_query;
    
    // 按优先级依次处理
    result = FullWidthToHalfWidth(result);  // 全角转半角
    result = NormalizeAlnum(result);         // 字母数字归一化
    result = ToLower(result);               // 转小写
    result = CollapseSpaces(result);         // 合并空格
    result = RemoveNoise(result);           // 去除噪声
    result = SpellCorrection(result);       // 拼写纠错
    result = RemoveStopWords(result);        // 去除停用词
    result = TraditionalToSimplified(result); // 繁体转简体（放在最后）
    
    return result;
}

void QueryNormalizer::Process(Session& session) const {
    const std::string& raw_query = session.qp_info.raw_query;
    std::string normalized = Normalize(raw_query);
    session.qp_info.normalized_query = normalized;
}

std::string QueryNormalizer::ToLower(const std::string& query) {
    return utils::ToLower(query);
}

std::string QueryNormalizer::TraditionalToSimplified(const std::string& query) {
    // 简化版本：实际项目中需使用完整繁简转换词典
    // 这里只处理常见繁体字符
    static const std::unordered_map<char32_t, char32_t> t2s_map = {
        // 常见繁体 -> 简体映射（部分示例）
        {U'國', U'国'}, {U'會', U'会'}, {U'時', U'时'},
        {U'長', U'长'}, {U'開', U'开'}, {U'車', U'车'},
        {U'門', U'门'}, {U'們', U'们'}, {U'問', U'问'},
        {U'電', U'电'}, {U'話', U'话'}, {U'東', U'东'},
        {U'圖', U'图'}, {U'書', U'书'}, {U'學', U'学'},
    };
    
    // 简化实现：直接返回原字符串
    // 实际项目应使用 OpenCC 等库
    return query;
}

std::string QueryNormalizer::FullWidthToHalfWidth(const std::string& query) {
    std::string result;
    for (size_t i = 0; i < query.length(); ++i) {
        unsigned char c = query[i];
        
        // 全角空格
        if (c == 0xEF && i + 2 < query.length()) {
            if (static_cast<unsigned char>(query[i+1]) == 0xBC &&
                static_cast<unsigned char>(query[i+2]) == 0x80) {
                result += ' ';
                i += 2;
                continue;
            }
        }
        
        // 全角字母数字（！到～：0xEFBC81 到 0xEFBE9E）
        if (c == 0xEF && i + 2 < query.length()) {
            unsigned char c1 = query[i+1];
            unsigned char c2 = query[i+2];
            if (c1 == 0xBC && c2 >= 0x81 && c2 <= 0xBF) {
                // 全角大写字母 Ａ-Ｚ: EF BC A1 ~ EF BC BA
                if (c2 >= 0xA1 && c2 <= 0xBA) {
                    result += (c2 - 0xA1 + 'A');
                    i += 2;
                    continue;
                }
                // 全角数字 ０-９: EF BC 90 ~ EF BC 99
                if (c2 >= 0x90 && c2 <= 0x99) {
                    result += (c2 - 0x90 + '0');
                    i += 2;
                    continue;
                }
            } else if (c1 == 0xBD && c2 >= 0x80 && c2 <= 0x9E) {
                // 全角小写字母 ａ-ｚ: EF BD 81 ~ EF BD 9A
                if (c2 >= 0x81 && c2 <= 0x9A) {
                    result += (c2 - 0x81 + 'a');
                    i += 2;
                    continue;
                }
            }
        }
        
        result += query[i];
    }
    return result;
}

std::string QueryNormalizer::RemoveNoise(const std::string& query) {
    std::string result;
    for (char c : query) {
        // 保留字母、数字、中文、空格、常见标点
        unsigned char uc = static_cast<unsigned char>(c);
        
        if (std::isalnum(uc) || 
            (uc >= 0x80) ||  // UTF-8 非ASCII字符
            c == ' ' || c == '\'' || c == '-' || c == '_') {
            result += c;
        }
        // 跳过 emoji 和其他特殊字符（简化判断）
    }
    return result;
}

std::string QueryNormalizer::RemoveStopWords(const std::string& query) {
    // 中英文停用词表（简化版）
    static const std::unordered_set<std::string> stop_words = {
        // 中文停用词
        "的", "了", "在", "是", "我", "有", "和", "就", "不", "人",
        "都", "一", "一个", "上", "也", "很", "到", "说", "要", "去",
        "你", "会", "着", "没有", "看", "好", "自己", "这",
        // 英文停用词
        "the", "is", "at", "of", "on", "and", "a", "an", "it", "to",
        "that", "this", "with", "for", "as", "was", "but", "be", "are"
    };
    
    // 简单实现：按空格分词后过滤
    std::vector<std::string> terms = utils::Split(query, ' ');
    std::string result;
    for (const auto& term : terms) {
        if (stop_words.find(utils::ToLower(term)) == stop_words.end()) {
            if (!result.empty()) result += " ";
            result += term;
        }
    }
    return result;
}

std::string QueryNormalizer::SpellCorrection(const std::string& query) {
    // 简化版本：去除3个以上连续重复字母（如"gooogle" -> "google"），保留正常双写
    std::string result;
    for (size_t i = 0; i < query.length(); ++i) {
        unsigned char uc = static_cast<unsigned char>(query[i]);
        // 只处理 ASCII 字母，且需要连续3个以上才跳过
        if (i >= 2 && uc < 0x80 && std::isalpha(uc) &&
            query[i] == query[i-1] && query[i-1] == query[i-2]) {
            continue;  // 跳过第3个及以上的连续重复字母
        }
        result += query[i];
    }
    return result;
}

std::string QueryNormalizer::NormalizeAlnum(const std::string& query) {
    // iPhone14 -> iphone 14
    std::string result;
    for (size_t i = 0; i < query.length(); ++i) {
        char c = query[i];
        // 字母后跟数字：加空格
        if (i > 0 && 
            std::isalpha(static_cast<unsigned char>(query[i-1])) &&
            std::isdigit(static_cast<unsigned char>(c))) {
            result += ' ';
        }
        // 数字后跟字母：加空格
        if (i > 0 &&
            std::isdigit(static_cast<unsigned char>(query[i-1])) &&
            std::isalpha(static_cast<unsigned char>(c))) {
            result += ' ';
        }
        result += c;
    }
    return result;
}

std::string QueryNormalizer::CollapseSpaces(const std::string& query) {
    std::string result;
    bool last_was_space = false;
    
    for (char c : query) {
        if (c == ' ' || c == '\t') {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }
    return utils::Trim(result);
}

bool QueryNormalizer::NeedsNormalization(const std::string& raw_query) const {
    // 检查是否需要归一化
    for (char c : raw_query) {
        unsigned char uc = static_cast<unsigned char>(c);
        // 全角字符
        if (uc > 127 && c != ' ') return true;
        // 大写字母
        if (std::isupper(uc)) return true;
    }
    return false;
}

} // namespace minisearchrec
