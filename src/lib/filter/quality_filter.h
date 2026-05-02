// ============================================================
// MiniSearchRec - 质量过滤器
// 过滤低质量内容
// ============================================================

#ifndef MINISEARCHREC_QUALITY_FILTER_H
#define MINISEARCHREC_QUALITY_FILTER_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class QualityFilterProcessor : public BaseFilterProcessor {
public:
    QualityFilterProcessor() = default;
    ~QualityFilterProcessor() override = default;

    bool ShouldKeep(const Session& session,
                    const DocCandidate& candidate) override;

    std::string Name() const override { return "QualityFilterProcessor"; }
    int Init(const YAML::Node& config) override;

private:
    float min_quality_score_ = 0.3f;
    int32_t min_click_count_ = 0;
    int32_t min_content_length_ = 50;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUALITY_FILTER_H
