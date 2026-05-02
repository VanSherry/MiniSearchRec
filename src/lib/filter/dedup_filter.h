// ============================================================
// MiniSearchRec - 去重过滤器
// 基于标题相似度去重
// ============================================================

#ifndef MINISEARCHREC_DEDUP_FILTER_H
#define MINISEARCHREC_DEDUP_FILTER_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class DedupFilterProcessor : public BaseFilterProcessor {
public:
    DedupFilterProcessor() = default;
    ~DedupFilterProcessor() override = default;

    bool ShouldKeep(const Session& session,
                    const DocCandidate& candidate) override;

    std::string Name() const override { return "DedupFilterProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    float similarity_threshold_ = 0.9f;  // 标题相似度阈值
};

} // namespace minisearchrec

#endif // MINISEARCHREC_DEDUP_FILTER_H
