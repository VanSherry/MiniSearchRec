// ============================================================
// MiniSearchRec - 质量分打分器
// 综合点击数、点赞数、质量评分
// ============================================================

#ifndef MINISEARCHREC_QUALITY_SCORER_H
#define MINISEARCHREC_QUALITY_SCORER_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class QualityScorerProcessor : public BaseScorerProcessor {
public:
    QualityScorerProcessor() = default;
    ~QualityScorerProcessor() override = default;

    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override;

    std::string Name() const override { return "QualityScorerProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    float click_weight_ = 0.3f;
    float like_weight_ = 0.4f;
    float quality_weight_ = 0.3f;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUALITY_SCORER_H
