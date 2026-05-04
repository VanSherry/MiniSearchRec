#include "lib/rank/scorer/weighted_sum_scorer.h"
#include "utils/logger.h"

namespace minisearchrec {

int WeightedSumScorer::Init(const rank::ProcessorConfig* config) {
    config_ = config;
    weight_ = config ? config->weight : 1.0f;
    features_.clear();

    if (config && config->params && config->params["features"]) {
        for (const auto& feat : config->params["features"]) {
            WeightedFeature wf;
            wf.name = feat["name"].as<std::string>("");
            wf.weight = feat["weight"].as<float>(1.0f);
            if (!wf.name.empty()) features_.push_back(std::move(wf));
        }
    }
    LOG_INFO("WeightedSumScorer::Init: weight={}, features={}", weight_, features_.size());
    return 0;
}

int WeightedSumScorer::Process(std::vector<rank::RankItem>& items) {
    if (features_.empty()) { LOG_WARN("WeightedSumScorer: no features configured"); return 0; }
    if (items.empty()) return 0;

    for (auto& item : items) {
        float total = 0.0f;
        for (const auto& fw : features_) {
            total += item.GetFeature(fw.name) * fw.weight;
        }
        item.score = total * weight_;
    }
    return 0;
}

REGISTER_RANK_PROCESSOR(WeightedSumScorer);
} // namespace minisearchrec
