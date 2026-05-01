// ============================================================
// MiniSearchRec - 垃圾内容过滤器（V1 阶段）
// ============================================================

#ifndef MINISEARCHREC_SPAM_FILTER_H
#define MINISEARCHREC_SPAM_FILTER_H

#include "core/processor.h"

namespace minisearchrec {

class SpamFilterProcessor : public BaseFilterProcessor {
public:
    SpamFilterProcessor() = default;
    ~SpamFilterProcessor() override = default;

    bool ShouldKeep(const Session& session,
                    const DocCandidate& candidate) override;

    std::string Name() const override { return "SpamFilterProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    float spam_threshold_ = 0.8f;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SPAM_FILTER_H
