#include "lib/rank/engine/rank_engine.h"
#include "utils/logger.h"
#include <chrono>

namespace minisearchrec {
namespace rank {

// ============================================================
// RankEngine::Score：执行 Processor 链
// ============================================================
void RankEngine::Score(std::vector<RankItem>& items,
                       const std::vector<ProcessorConfig>& processor_configs) {
    LOG_INFO("RankEngine::Score: {} processors, {} items",
             processor_configs.size(), items.size());

    for (const auto& proc_cfg : processor_configs) {
        auto* raw = ProcessorRegistry::Instance().Create(proc_cfg.name);
        if (!raw) {
            LOG_WARN("RankEngine: processor '{}' not found, skip", proc_cfg.name);
            continue;
        }
        auto proc = std::unique_ptr<ProcessorInterface>(raw);

        auto start = std::chrono::steady_clock::now();
        if (proc->Init(&proc_cfg) != 0) {
            LOG_WARN("RankEngine: processor '{}' init failed, skip", proc_cfg.name);
            continue;
        }

        int ret = proc->Process(items);
        auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (ret != 0) {
            LOG_ERROR("RankEngine: processor '{}' failed, ret={}", proc_cfg.name, ret);
            return;
        }
        LOG_INFO("  [{}] cost={}us", proc_cfg.name, cost_us);
    }
}

// ============================================================
// YAML 解析辅助
// ============================================================
static std::vector<ProcessorConfig> ParseProcessorList(const YAML::Node& node) {
    std::vector<ProcessorConfig> procs;
    if (!node || !node.IsSequence()) return procs;

    for (const auto& item : node) {
        ProcessorConfig cfg;
        cfg.name = item["name"].as<std::string>("");
        if (cfg.name.empty()) continue;
        cfg.weight = item["weight"].as<float>(1.0f);
        cfg.params = item["params"] ? item["params"] : YAML::Node(YAML::NodeType::Map);
        procs.push_back(std::move(cfg));
    }
    return procs;
}

static StageConfig ParseStageConfig(const YAML::Node& node) {
    StageConfig cfg;
    if (!node) return cfg;
    cfg.top_k = node["top_k"].as<int>(-1);
    cfg.processors = ParseProcessorList(node["processors"]);
    return cfg;
}

// ============================================================
// RankConfigManager
// ============================================================
bool RankConfigManager::LoadFromConfig(const YAML::Node& yaml,
                                        const std::string& business_type) {
    if (!yaml["rank_config"]) return false;

    const auto& rc = yaml["rank_config"];
    BusinessConfig biz_cfg;
    biz_cfg.rank   = ParseStageConfig(rc["rank"]);
    biz_cfg.rerank = ParseStageConfig(rc["rerank"]);

    configs_[business_type] = std::move(biz_cfg);
    LOG_INFO("RankConfigManager: loaded '{}': rank(top_k={}, {}procs), rerank(top_k={}, {}procs)",
             business_type,
             biz_cfg.rank.top_k, biz_cfg.rank.processors.size(),
             biz_cfg.rerank.top_k, biz_cfg.rerank.processors.size());
    return true;
}

const StageConfig* RankConfigManager::GetStageConfig(
    const std::string& business_type, const std::string& stage) const {
    auto it = configs_.find(business_type);
    if (it == configs_.end()) return nullptr;

    if (stage == "rerank") return &it->second.rerank;
    return &it->second.rank;  // 默认 "rank"
}

} // namespace rank
} // namespace minisearchrec
