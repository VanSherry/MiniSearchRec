// ============================================================
// MiniSearchRec - 热门内容召回处理器
// 推荐近期热门（高点击/高点赞）内容
// ============================================================

#ifndef MINISEARCHREC_HOT_CONTENT_RECALL_H
#define MINISEARCHREC_HOT_CONTENT_RECALL_H

#include "core/processor.h"

namespace minisearchrec {

class HotContentRecallProcessor : public BaseRecallProcessor {
public:
    HotContentRecallProcessor() = default;
    ~HotContentRecallProcessor() override = default;

    int Process(Session& session) override;
    std::string Name() const override { return "HotContentRecallProcessor"; }
    bool Init(const YAML::Node& config) override;

private:
    int max_recall_ = 100;
    int time_window_hours_ = 24;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HOT_CONTENT_RECALL_H
