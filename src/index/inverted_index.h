// ===========================================================
// MiniSearchRec - 倒排索引
// 参考：微信 WXSearch SU、X(Twitter) Earlybird
// 作用：支持关键词快速检索，是搜索系统的基础
// ===========================================================

#ifndef MINISEARCHREC_INVERTED_INDEX_H
#define MINISEARCHREC_INVERTED_INDEX_H

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <cmath>
#include <memory>

// 条件编译：如果定义了 HAVE_CPPJIEBA，则包含 cppjieba 头文件
#ifdef HAVE_CPPJIEBA
#include <cppjieba/Jieba.hpp>
#endif

namespace minisearchrec {

// ===========================================================
// 倒排链节点
// 对应微信搜推的 PostingNode
// ===========================================================
struct PostingNode {
    std::string doc_id;
    uint32_t term_freq = 0;       // 词在文档中出现频率
    float field_weight = 1.0f;    // 字段权重（title > content > tags）
    uint32_t doc_len = 0;         // 文档长度（用于 BM25）
};

// ===========================================================
// 倒排索引核心结构
// Term -> [(doc_id, term_freq, field_weight), ...]
// ===========================================================
class InvertedIndex {
public:
    InvertedIndex() = default;
    ~InvertedIndex() = default;

    // 设置 cppjieba 分词器（可选，不调用则使用简单分词）
    void SetJieba(const std::string& dict_path,
                  const std::string& model_path,
                  const std::string& user_dict_path,
                  const std::string& idf_path);

    // 添加文档到索引
    void AddDocument(const std::string& doc_id,
                    const std::string& title,
                    const std::string& content,
                    const std::string& category,
                    const std::vector<std::string>& tags,
                    int32_t content_length);

    // 批量添加文档
    void AddDocuments(const std::vector<std::string>& doc_ids,
                      const std::vector<std::string>& titles,
                      const std::vector<std::string>& contents,
                      const std::vector<std::string>& categories,
                      const std::vector<std::vector<std::string>>& tags_list,
                      const std::vector<int32_t>& content_lengths);

    // 检索：返回包含任意 term 的文档列表（OR 语义）
    std::vector<std::string> Search(
        const std::vector<std::string>& terms,
        int max_results = 1000
    );

    // 检索：返回包含所有 term 的文档列表（AND 语义）
    std::vector<std::string> SearchAnd(
        const std::vector<std::string>& terms
    );

    // 获取文档的倒排链信息（用于打分）
    std::vector<PostingNode> GetPostings(
        const std::string& term,
        const std::string& doc_id
    ) const;

    // 获取单个文档的所有 term 频率（用于 BM25 打分）
    std::unordered_map<std::string, PostingNode> GetDocPostings(
        const std::string& doc_id
    ) const;

    // 计算 IDF（Inverse Document Frequency）
    float CalculateIDF(const std::string& term) const;

    // 获取平均文档长度
    float GetAvgDocLen() const { return avg_doc_len_; }

    // 持久化到磁盘
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    // 获取统计信息
    size_t GetDocCount() const { return doc_lengths_.size(); }
    size_t GetTermCount() const { return index_.size(); }

    // 清空索引
    void Clear();

private:
    // 分词方法：优先使用 cppjieba，否则降级为简单分词
    std::vector<std::string> Tokenize(const std::string& text);

    // 简单分词（降级方案）
    std::vector<std::string> SimpleTokenize(const std::string& text) const;

    // 核心倒排映射：Term -> 倒排链
    std::unordered_map<std::string, std::vector<PostingNode>> index_;

    // 文档长度表（用于 BM25 归一化）
    std::unordered_map<std::string, uint32_t> doc_lengths_;

    // 文档 -> 包含的词列表（用于快速获取文档所有词）
    std::unordered_map<std::string, std::vector<std::string>> doc_terms_;

    // 平均文档长度（BM25 需要）
    float avg_doc_len_ = 0.0f;

    // 总文档数
    size_t total_docs_ = 0;

    // cppjieba 分词器（可选）
#ifdef HAVE_CPPJIEBA
    std::unique_ptr<cppjieba::Jieba> jieba_;
#endif

    // 读写锁，支持并发读
    mutable std::shared_mutex rwlock_;

    // 重新计算平均文档长度
    void RecalculateAvgDocLen();
};

} // namespace minisearchrec

#endif // MINISEARCHREC_INVERTED_INDEX_H
