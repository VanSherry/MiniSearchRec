// ============================================================
// MiniSearchRec - 黑名单过滤器（V1 阶段）
// ============================================================

#ifndef MINISEARCHREC_BLACKLIST_FILTER_H
#define MINISEARCHREC_BLACKLIST_FILTER_H

#include "framework/processor/processor_interface.h"
#include "biz/search/search_session.h"
#include <unordered_set>

namespace minisearchrec {

class BlacklistFilterProcessor : public BaseFilterProcessor {
public:
    BlacklistFilterProcessor() = default;
    ~BlacklistFilterProcessor() override = default;

    bool ShouldKeep(const Session& session,
                    const DocCandidate& candidate) override;

    std::string Name() const override { return "BlacklistFilterProcessor"; }
    int Init(const YAML::Node& config) override;

private:
    std::unordered_set<std::string> blacklist_ids_;
    std::string blacklist_file_;

    bool LoadBlacklist(const std::string& file_path);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_BLACKLIST_FILTER_H
