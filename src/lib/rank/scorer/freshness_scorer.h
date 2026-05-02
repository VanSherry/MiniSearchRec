// ============================================================
// MiniSearchRec - 时效性（新鲜度）打分器
// 新发布的内容获得更高分数
// ============================================================

#ifndef MINISEARCHREC_FRESHNESS_SCORER_H
#define MINISEARCHREC_FRESHNESS_SCORER_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"

namespace minisearchrec {

class FreshnessScorerProcessor : public BaseScorerProcessor {
public:
    FreshnessScorerProcessor() = default;
    ~FreshnessScorerProcessor() override = default;

    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override;

    std::string Name() const override { return "FreshnessScorerProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    float weight_ = 1.0f;
    int max_age_days_ = 365;    // 超过此天数的文档时效分为 0
    float decay_rate_ = 0.01f; // 衰减速率
};

} // namespace minisearchrec

#endif // MINISEARCHREC_FRESHNESS_SCORER_H
