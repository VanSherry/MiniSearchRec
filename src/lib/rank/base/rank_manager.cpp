// ============================================================
// MiniSearchRec - RankManager 实现
// ============================================================

#include "lib/rank/base/rank_manager.h"
#include "utils/logger.h"

namespace minisearchrec {
namespace rank {

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
    for (const auto& proc : config.processors) {
        if (!ProcessorRegistry::Instance().Has(proc.name)) {
            LOG_WARN("RankManager::RegisterBusiness: processor '{}' not registered (will skip at runtime)",
                     proc.name);
        }
    }

    configs_[config.business_type] = config;
    LOG_INFO("RankManager: registered business='{}', factory='{}', processors={}",
             config.business_type, config.factory_name, config.processors.size());
}

const RankFactory* RankManager::GetFactory(const std::string& business_type) const {
    auto it = configs_.find(business_type);
    if (it == configs_.end()) {
        LOG_ERROR("RankManager::GetFactory: business_type '{}' not found", business_type);
        return nullptr;
    }
    return RankFactoryRegistry::Instance().GetSingleton(it->second.factory_name);
}

std::vector<ProcessorConfig> RankManager::GetProcessors(const std::string& business_type) const {
    auto it = configs_.find(business_type);
    if (it == configs_.end()) {
        LOG_WARN("RankManager::GetProcessors: business_type '{}' not found", business_type);
        return {};
    }
    return it->second.processors;
}

} // namespace rank
} // namespace minisearchrec
