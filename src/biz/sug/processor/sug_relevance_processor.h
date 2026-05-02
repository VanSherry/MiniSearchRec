// ============================================================
// MiniSearchRec - Sug 相关性评分 + 过滤 Processor
// 对标：RelevanceFilterProcessor + ExtractFeatureProcessor
// 计算前缀覆盖率、编辑距离归一化、文本相关性融合分
// ============================================================

#ifndef MINISEARCHREC_SUG_RELEVANCE_PROCESSOR_H
#define MINISEARCHREC_SUG_RELEVANCE_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {

class SugRelevanceProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "SugRelevanceProcessor"; }

private:
    // 编辑距离（UTF-8 字符级别）
    static int EditDistance(const std::string& a, const std::string& b);
    // 覆盖率（query 中有多少字符出现在 word 中）
    static float CoverRatio(const std::string& query, const std::string& word);
};

} // namespace minisearchrec

#endif
