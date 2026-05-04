#include "lib/rank/base/rank_manager.h"
#include "utils/logger.h"

namespace minisearchrec {
namespace rank {

// ============================================================
// 辅助：从 YAML 节点解析 ProcessorConfig 列表
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

// ============================================================
// LoadFromConfig：从 YAML 加载业务排序配置
// ============================================================
bool RankManager::LoadFromConfig(const YAML::Node& yaml,
                                  const std::string& business_type) {
    if (!yaml["rank_config"]) return false;  // 没有 rank_config 段，不报错

    const auto& rc = yaml["rank_config"];

    BusinessRankConfig cfg;
    cfg.business_type = business_type;
    cfg.factory_name = rc["factory"].as<std::string>("");
    if (cfg.factory_name.empty()) {
        LOG_WARN("RankManager::LoadFromConfig: '{}' has rank_config but no factory name",
                 business_type);
        return false;
    }

    cfg.coarse_processors = ParseProcessorList(rc["coarse_processors"]);
    cfg.fine_processors = ParseProcessorList(rc["fine_processors"]);

    RegisterBusiness(cfg);
    LOG_INFO("RankManager: loaded '{}' from config: factory={}, coarse={}, fine={}",
             business_type, cfg.factory_name,
             cfg.coarse_processors.size(), cfg.fine_processors.size());
    return true;
}

void RankManager::RegisterBusiness(const BusinessRankConfig& config) {
    if (config.business_type.empty() || config.factory_name.empty()) {
        LOG_ERROR("RankManager::RegisterBusiness: empty business_type or factory_name");
        return;
    }

    // 验证 Factory 存在
    auto* factory = RankFactoryRegistry::Instance().GetSingleton(config.factory_name);
    if (!factory) {
        LOG_ERROR("RankManager::RegisterBusiness: factory '{}' not found for business '{}'",
                  config.factory_name, config.business_type);
        return;
    }

    // 验证 Processor 存在
    auto check_procs = [&](const std::vector<ProcessorConfig>& procs, const std::string& stage) {
        for (const auto& proc : procs) {
            if (!ProcessorRegistry::Instance().Has(proc.name)) {
                LOG_WARN("RankManager: processor '{}' not registered for '{}' stage (will skip at runtime)",
                         proc.name, stage);
            }
        }
    };
    check_procs(config.coarse_processors, "coarse");
    check_procs(config.fine_processors, "fine");

    configs_[config.business_type] = config;
    LOG_INFO("RankManager: registered business='{}', factory='{}', coarse={}, fine={}",
             config.business_type, config.factory_name,
             config.coarse_processors.size(), config.fine_processors.size());
}

const RankFactory* RankManager::GetFactory(const std::string& business_type) const {
    auto it = configs_.find(business_type);
    if (it == configs_.end()) {
        LOG_ERROR("RankManager::GetFactory: business_type '{}' not found", business_type);
        return nullptr;
    }
    return RankFactoryRegistry::Instance().GetSingleton(it->second.factory_name);
}

std::vector<ProcessorConfig> RankManager::GetProcessors(
    const std::string& business_type, const std::string& stage) const {
    auto it = configs_.find(business_type);
    if (it == configs_.end()) {
        LOG_WARN("RankManager::GetProcessors: business_type '{}' not found", business_type);
        return {};
    }
    if (stage == "fine") return it->second.fine_processors;
    return it->second.coarse_processors;  // 默认 "coarse"
}

} // namespace rank
} // namespace minisearchrec
