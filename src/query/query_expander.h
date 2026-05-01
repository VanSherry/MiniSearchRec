// ============================================================
// MiniSearchRec - 查询扩展
// 参考：微信 QP 同义词扩展、X(Twitter) Query Expansion
// 作用：通过同义词、相关词扩展 Query，提升召回率
// ============================================================

#ifndef MINISEARCHREC_QUERY_EXPANDER_H
#define MINISEARCHREC_QUERY_EXPANDER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "../core/session.h"

namespace minisearchrec {

// ============================================================
// 查询扩展处理器
// 对归一化后的 Query 进行扩展
// ============================================================
class QueryExpander {
public:
    QueryExpander() = default;
    ~QueryExpander() = default;

    // 扩展主函数
    // 输入：归一化后的查询词列表
    // 输出：扩展后的查询词列表（包含同义词、相关词）
    std::vector<std::string> Expand(const std::vector<std::string>& terms) const;

    // 完整处理（更新 Session 中的 terms）
    void Process(Session& session) const;

    // 具体扩展方法
    // 1. 同义词扩展
    static std::vector<std::string> AddSynonyms(const std::vector<std::string>& terms);

    // 2. 相关词扩展（基于统计）
    static std::vector<std::string> AddRelatedTerms(const std::vector<std::string>& terms);

    // 3. 缩写扩展（AI -> Artificial Intelligence）
    static std::vector<std::string> ExpandAbbreviations(const std::vector<std::string>& terms);

    // 4. 类别词扩展（手机 -> 智能手机、移动电话）
    static std::vector<std::string> AddCategoryTerms(const std::vector<std::string>& terms);

    // 5. 去除停用扩展词（避免扩展太多）
    static std::vector<std::string> FilterExpansion(const std::vector<std::string>& original,
                                                    const std::vector<std::string>& expanded);

    // 加载同义词词典
    bool LoadSynonymDict(const std::string& dict_path);

    // 加载相关词词典
    bool LoadRelatedTermDict(const std::string& dict_path);

private:
    // 同义词词典：词 -> [同义词列表]
    std::unordered_map<std::string, std::vector<std::string>> synonym_dict_;

    // 相关词词典：词 -> [相关词列表]
    std::unordered_map<std::string, std::vector<std::string>> related_term_dict_;

    // 缩写词典：缩写 -> 全称
    std::unordered_map<std::string, std::string> abbreviation_dict_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUERY_EXPANDER_H
