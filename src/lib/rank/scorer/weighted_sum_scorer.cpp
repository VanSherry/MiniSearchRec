#include "lib/rank/scorer/weighted_sum_scorer.h"
#include "utils/logger.h"

namespace minisearchrec {

int WeightedSumScorer::Init(rank::RankContextPtr ctx, const rank::ProcessorConfig* config) {
    ctx_ = std::move(ctx);
    config_ = config;
    if (!config_) return -1;
    weight_ = config_->weight;
    features_.clear();

    if (config_->params && config_->params["features"]) {
        for (const auto& feat : config_->params["features"]) {
            WeightedFeature wf;
            wf.name = feat["name"].as<std::string>("");
            wf.weight = feat["weight"].as<float>(1.0f);
            if (!wf.name.empty()) {
                features_.push_back(std::move(wf));
            }
        }
    }
    LOG_INFO("WeightedSumScorer::Init: weight={}, features={}", weight_, features_.size());
    return 0;
}

int WeightedSumScorer::Process() {
    if (features_.empty()) {
        LOG_WARN("WeightedSumScorer: no features configured");
        return 0;
    }

    auto vec = ctx_->GetVector();
    if (!vec || vec->Size() == 0) return 0;

    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = vec->GetItem(i).get();
        if (!item) continue;

        float total = 0.0f;
        for (const auto& fw : features_) {
            total += item->GetFeature(fw.name) * fw.weight;
        }
        total *= weight_;
        item->SetScore(total);
    }

    return 0;
}

REGISTER_RANK_PROCESSOR(WeightedSumScorer);

} // namespace minisearchrec
