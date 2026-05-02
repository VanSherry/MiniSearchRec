// ============================================================
// MiniSearchRec - HandlerManager 实现
// 对标：通用搜索框架 HandlerManager
// ============================================================

#include "framework/handler/handler_manager.h"
#include "framework/class_register.h"
#include "framework/session/session_factory.h"
#include "utils/logger.h"
#include <yaml-cpp/yaml.h>

namespace minisearchrec {
namespace framework {

// ============================================================
// InitFromConfig：从 YAML 配置文件读取 business 列表，自动反射创建 Handler
// 对标通用搜索框架 HandlerManager::Init
// ============================================================
int32_t HandlerManager::InitFromConfig(const std::string& config_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(config_path);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("HandlerManager::InitFromConfig: failed to load '{}': {}",
                  config_path, e.what());
        return -1;
    }

    if (!root["businesses"] || !root["businesses"].IsSequence()) {
        LOG_ERROR("HandlerManager::InitFromConfig: 'businesses' not found or not a list in '{}'",
                  config_path);
        return -2;
    }

    std::vector<BusinessConfig> configs;
    for (const auto& node : root["businesses"]) {
        BusinessConfig cfg;
        cfg.business_type = node["business_type"].as<std::string>("");
        cfg.handler_name = node["handler_name"].as<std::string>("");
        cfg.session_name = node["session_name"].as<std::string>("Session");
        cfg.skip_search = node["skip_search"].as<bool>(false);
        cfg.skip_rank = node["skip_rank"].as<bool>(false);
        cfg.skip_interpose = node["skip_interpose"].as<bool>(false);

        // 解析 extra_config
        if (node["extra"] && node["extra"].IsMap()) {
            for (auto it = node["extra"].begin(); it != node["extra"].end(); ++it) {
                cfg.extra_config[it->first.as<std::string>()] = it->second.as<std::string>("");
            }
        }

        if (cfg.business_type.empty() || cfg.handler_name.empty()) {
            LOG_WARN("HandlerManager::InitFromConfig: skip entry with empty business_type or handler_name");
            continue;
        }

        configs.push_back(cfg);

        // 同时注册 SessionFactory
        SessionFactory::Instance().Register(cfg.business_type, cfg.session_name);
    }

    LOG_INFO("HandlerManager::InitFromConfig: parsed {} businesses from '{}'",
             configs.size(), config_path);

    return Init(configs);
}

int32_t HandlerManager::Init(const std::vector<BusinessConfig>& configs) {
    configs_ = configs;

    for (const auto& config : configs_) {
        if (config.business_type.empty()) {
            LOG_ERROR("HandlerManager::Init: empty business_type in config");
            return -1;
        }
        if (config.handler_name.empty()) {
            LOG_ERROR("HandlerManager::Init: empty handler_name for business_type '{}'",
                      config.business_type);
            return -2;
        }

        // 通过反射获取 Handler 单例
        auto* handler = ClassRegistry<BaseHandler>::Instance().GetSingleton(config.handler_name);
        if (!handler) {
            LOG_ERROR("HandlerManager::Init: handler '{}' not registered (forgot REGISTER_MSR_HANDLER?)",
                      config.handler_name);
            return -3;
        }

        // 初始化 Handler
        int32_t ret = handler->Init(config);
        if (ret != 0) {
            LOG_ERROR("HandlerManager::Init: handler '{}' Init failed, ret={}",
                      config.handler_name, ret);
            return -4;
        }

        // 注册到路由表
        if (handler_map_.count(config.business_type)) {
            LOG_WARN("HandlerManager::Init: business_type '{}' already registered, overwriting",
                     config.business_type);
        }
        handler_map_[config.business_type] = handler;

        LOG_INFO("HandlerManager::Init: registered business_type='{}' → handler='{}'",
                 config.business_type, config.handler_name);
    }

    LOG_INFO("HandlerManager::Init: total {} handlers registered", handler_map_.size());
    return 0;
}

int32_t HandlerManager::RegisterHandler(const BusinessConfig& config, BaseHandler* handler) {
    if (!handler) {
        LOG_ERROR("HandlerManager::RegisterHandler: null handler for '{}'", config.business_type);
        return -1;
    }

    int32_t ret = handler->Init(config);
    if (ret != 0) {
        LOG_ERROR("HandlerManager::RegisterHandler: Init failed for '{}', ret={}",
                  config.handler_name, ret);
        return ret;
    }

    handler_map_[config.business_type] = handler;
    configs_.push_back(config);

    LOG_INFO("HandlerManager::RegisterHandler: '{}' → '{}'",
             config.business_type, config.handler_name);
    return 0;
}

const BaseHandler* HandlerManager::GetHandler(const std::string& business_type) const {
    auto it = handler_map_.find(business_type);
    if (it == handler_map_.end()) {
        LOG_WARN("HandlerManager: no handler for business_type '{}'", business_type);
        return nullptr;
    }
    return it->second;
}

const BaseHandler* HandlerManager::GetHandler(uint64_t business_type) const {
    return GetHandler(std::to_string(business_type));
}

std::vector<std::string> HandlerManager::GetAllBusinessTypes() const {
    std::vector<std::string> types;
    types.reserve(handler_map_.size());
    for (const auto& [k, v] : handler_map_) {
        types.push_back(k);
    }
    return types;
}

} // namespace framework
} // namespace minisearchrec
