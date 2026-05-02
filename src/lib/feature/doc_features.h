// ==========================================================
// MiniSearchRec - 文档特征提取
// 参考：业界文档特征方案
// ==========================================================

#ifndef MINISEARCHREC_DOC_FEATURES_H
#define MINISEARCHREC_DOC_FEATURES_H

#include <string>
#include <vector>
#include <unordered_map>
#include "biz/search/search_session.h"
#include "../index/doc_store.h"

namespace minisearchrec {

// ==========================================================
// 文档特征提取器
// 从候选文档中提取各类特征，用于精排模型
// ==========================================================
class DocFeatures {
public:
    DocFeatures() = default;
    ~DocFeatures() = default;

    // 提取单篇文档的所有特征
    void Extract(const DocCandidate& doc,
                 const Session& session,
                 std::unordered_map<std::string, float>& features) const;

    // 批量提取文档特征
    void BatchExtract(const std::vector<DocCandidate>& docs,
                      const Session& session,
                      std::vector<std::unordered_map<std::string, float>>& features) const;

    // 具体特征提取方法
    // 文档质量分
    static float QualityScore(const DocCandidate& doc);

    // 文档时效性（发布时间距离现在）
    static float Freshness(const DocCandidate& doc, int64_t now_ms);

    // 文档点击数
    static float ClickCount(const DocCandidate& doc);

    // 文档点赞数
    static float LikeCount(const DocCandidate& doc);

    // 文档标题长度
    static float TitleLength(const DocCandidate& doc);

    // 文档内容长度（从 doc_store 获取）
    static float ContentLength(const std::string& doc_id,
                               const DocStore& doc_store);

    // 文档类别匹配度（与查询类别）
    static float CategoryMatch(const DocCandidate& doc,
                               const std::string& inferred_category);

    // 文档关键词覆盖率（与查询词）
    static float KeywordCoverage(const DocCandidate& doc,
                                  const std::vector<std::string>& terms);

    // 文档热度（基于点击、点赞等）
    static float DocPopularity(const DocCandidate& doc);

    // 文档权威性（基于作者、来源等）
    static float Authority(const DocCandidate& doc);

    // 文档阅读难度（基于内容长度、词汇复杂度等）
    static float Readability(const DocCandidate& doc);

    // 提取单个特征（按名称）
    float ExtractFeature(const std::string& feature_name,
                         const DocCandidate& doc,
                         const Session& session) const;

    // 获取特征名称列表
    std::vector<std::string> GetFeatureNames() const;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_DOC_FEATURES_H
