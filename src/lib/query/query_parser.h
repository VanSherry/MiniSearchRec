// ===========================================================
// MiniSearchRec - Query 解析器
// 参考：业界 Query Parser 方案
// ===========================================================

#ifndef MINISEARCHREC_QUERY_PARSER_H
#define MINISEARCHREC_QUERY_PARSER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "biz/search/search_session.h"

namespace minisearchrec {

// ===========================================================
// Query 解析器
// 对用户输入的查询词进行分词、纠错、归一化
// ===========================================================
class QueryParser {
public:
    QueryParser() = default;
    ~QueryParser() = default;

    // 解析 Query，结果写入 qp_info
    void Parse(const std::string& raw_query, QPInfo& qp_info) const;

    // 验证 Query 合法性
    bool ValidateQuery(const std::string& raw_query, std::string& error_msg) const;

    // 分词
    std::vector<std::string> Tokenize(const std::string& text) const;

    // 推断分类
    std::string InferCategory(const std::vector<std::string>& terms) const;

    // 设置停用词
    void SetStopWords(const std::vector<std::string>& stop_words);

    // 设置同义词词典
    void SetSynonymDict(
        const std::unordered_map<std::string, std::vector<std::string>>& synonym_dict);

private:
    std::vector<std::string> stop_words_;
    std::unordered_map<std::string, std::vector<std::string>> synonym_dict_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUERY_PARSER_H
