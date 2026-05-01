// ============================================================
// MiniSearchRec - 查询解析器
// 参考：微信 QP 模块、X(Twitter) QueryParser
// 作用：将原始查询解析为结构化 QPInfo
// ============================================================

#include "query/query_parser.h"
#include "query/query_normalizer.h"
#include "query/query_expander.h"
#include "utils/string_utils.h"
#include "core/app_context.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>

namespace minisearchrec {

void QueryParser::Parse(const std::string& raw_query, QPInfo& qp_info) const {
    // 1. 设置原始查询
    qp_info.raw_query = raw_query;
    
    // 2. 归一化
    QueryNormalizer normalizer;
    std::string normalized = normalizer.Normalize(raw_query);
    qp_info.normalized_query = normalized;
    
    // 3. 分词
    qp_info.terms = Tokenize(normalized);
    
    // 4. 查询扩展
    QueryExpander expander;
    qp_info.terms = expander.Expand(qp_info.terms);
    
    // 5. 推断类别
    qp_info.inferred_category = InferCategory(qp_info.terms);
    
    // 6. 计算 IDF（需要倒排索引，这里先初始化为 1.0）
    for (const auto& term : qp_info.terms) {
        qp_info.term_idf[term] = 1.0f;  // 实际项目需从索引获取
    }

    // 7. 通过 EmbeddingProvider 生成 query embedding（配置驱动，一键切换）
    //    若已有外部服务填充了 query_embedding，跳过此步。
    if (qp_info.query_embedding.empty() && !qp_info.terms.empty()) {
        auto provider = AppContext::Instance().GetEmbeddingProvider();
        qp_info.query_embedding = provider->Encode(raw_query);
    }
}

std::vector<std::string> QueryParser::Tokenize(const std::string& text) const {
    return utils::Tokenize(text);
}

std::string QueryParser::InferCategory(const std::vector<std::string>& terms) const {
    // 简化实现：基于关键词匹配
    static const std::unordered_map<std::string, std::string> keyword_category = {
        {"手机", "technology"},
        {"电脑", "technology"},
        {"AI", "technology"},
        {"美食", "food"},
        {"餐厅", "food"},
        {"旅游", "travel"},
        {"酒店", "travel"},
    };
    
    for (const auto& term : terms) {
        auto it = keyword_category.find(term);
        if (it != keyword_category.end()) {
            return it->second;
        }
    }
    
    return "general";
}

void QueryParser::SetStopWords(const std::vector<std::string>& stop_words) {
    stop_words_ = stop_words;
}

void QueryParser::SetSynonymDict(
    const std::unordered_map<std::string, std::vector<std::string>>& synonym_dict) {
    synonym_dict_ = synonym_dict;
}

bool QueryParser::ValidateQuery(const std::string& raw_query,
                                std::string& error_msg) const {
    if (raw_query.empty()) {
        error_msg = "Query is empty";
        return false;
    }
    
    if (raw_query.length() > 200) {
        error_msg = "Query too long (max 200 characters)";
        return false;
    }
    
    // 检查是否包含非法字符（简化）
    for (char c : raw_query) {
        if (c == '\0' || c == '\n' || c == '\r') {
            error_msg = "Query contains invalid characters";
            return false;
        }
    }
    
    return true;
}

} // namespace minisearchrec
