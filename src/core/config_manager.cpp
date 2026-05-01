// =========================================================
// MiniSearchRec - 配置管理器实现
// =========================================================

#include "core/config_manager.h"
#include "utils/logger.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace minisearchrec {

bool ConfigManager::LoadAll(const std::string& config_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_dir_ = config_dir;

    bool ok = true;
    ok = ok && LoadGlobalConfig(config_dir + "/global.yaml");
    ok = ok && LoadRecallConfig(config_dir + "/recall.yaml");
    ok = ok && LoadRankConfig(config_dir + "/rank.yaml");
    ok = ok && LoadFilterConfig(config_dir + "/filter.yaml");
    return ok;
}

bool ConfigManager::LoadGlobalConfig(const std::string& path) {
    try {
        YAML::Node config = YAML::LoadFile(path);
        if (config.IsNull()) {
            LOG_ERROR("Failed to load config file: {} - file is empty or invalid YAML", path);
            return false;
        }

        if (config["server"]) {
            global_config_.server.port =
                config["server"]["port"].as<int>(8080);
            global_config_.server.worker_threads =
                config["server"]["worker_threads"].as<int>(4);
            global_config_.server.request_timeout_ms =
                config["server"]["request_timeout_ms"].as<int>(200);
            global_config_.server.host =
                config["server"]["host"].as<std::string>("0.0.0.0");
        }

        if (config["index"]) {
            global_config_.index.data_dir =
                config["index"]["data_dir"].as<std::string>("./data/");
            global_config_.index.index_dir =
                config["index"]["index_dir"].as<std::string>("./index/");
            global_config_.index.rebuild_on_start =
                config["index"]["rebuild_on_start"].as<bool>(false);
            global_config_.index.rebuild_batch_size =
                config["index"]["rebuild_batch_size"].as<int>(1000);
        }

        if (config["logging"]) {
            global_config_.log.level =
                config["logging"]["level"].as<std::string>("info");
            global_config_.log.file =
                config["logging"]["file"].as<std::string>("./logs/minisearchrec.log");
            global_config_.log.max_size_mb =
                config["logging"]["max_size_mb"].as<int>(100);
            global_config_.log.max_files =
                config["logging"]["max_files"].as<int>(5);
        }

        if (config["cache"]) {
            global_config_.cache.enable =
                config["cache"]["enable"].as<bool>(true);
            global_config_.cache.local_capacity =
                config["cache"]["local_capacity"].as<int>(100);
            global_config_.cache.redis_host =
                config["cache"]["redis_host"].as<std::string>("127.0.0.1");
            global_config_.cache.redis_port =
                config["cache"]["redis_port"].as<int>(6379);
            global_config_.cache.default_ttl_seconds =
                config["cache"]["default_ttl_seconds"].as<int>(300);
        }

        // 配置合法性校验
        if (!ValidateGlobalConfig()) {
            LOG_ERROR("Global config validation failed for file: {}", path);
            return false;
        }

        LOG_INFO("Successfully loaded global config from: {}", path);

    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to load {}: {}", path, e.what());
        return false;
    }
    return true;
}

bool ConfigManager::LoadRecallConfig(const std::string& path) {
    try {
        recall_config_ = YAML::LoadFile(path);
        LOG_INFO("Successfully loaded recall config from: {}", path);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to load {}: {}", path, e.what());
        return false;
    }
    return true;
}

bool ConfigManager::LoadRankConfig(const std::string& path) {
    try {
        rank_config_ = YAML::LoadFile(path);
        LOG_INFO("Successfully loaded rank config from: {}", path);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to load {}: {}", path, e.what());
        return false;
    }
    return true;
}

bool ConfigManager::LoadFilterConfig(const std::string& path) {
    try {
        filter_config_ = YAML::LoadFile(path);
        LOG_INFO("Successfully loaded filter config from: {}", path);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to load {}: {}", path, e.what());
        return false;
    }
    return true;
}

bool ConfigManager::Reload() {
    LOG_INFO("Reloading all configs from: {}", config_dir_);
    return LoadAll(config_dir_);
}

// =========================================================
// 配置校验
// =========================================================

bool ConfigManager::ValidateGlobalConfig() {
    bool valid = true;

    if (!ValidatePort(global_config_.server.port, "server.port")) {
        valid = false;
    }

    if (!ValidatePositive(global_config_.server.worker_threads, "server.worker_threads")) {
        valid = false;
    }

    if (!ValidatePositive(global_config_.server.request_timeout_ms, "server.request_timeout_ms")) {
        valid = false;
    }

    if (!ValidateLogLevel(global_config_.log.level)) {
        valid = false;
    }

    if (!ValidatePositive(global_config_.cache.local_capacity, "cache.local_capacity")) {
        valid = false;
    }

    if (!ValidatePort(global_config_.cache.redis_port, "cache.redis_port")) {
        valid = false;
    }

    if (!ValidatePositive(global_config_.index.rebuild_batch_size, "index.rebuild_batch_size")) {
        valid = false;
    }

    return valid;
}

bool ConfigManager::ValidatePort(int port, const std::string& field_name) {
    if (port < 1 || port > 65535) {
        LOG_ERROR("Invalid {}: {}. Must be in range [1, 65535]", field_name, port);
        return false;
    }
    return true;
}

bool ConfigManager::ValidatePositive(int value, const std::string& field_name) {
    if (value <= 0) {
        LOG_ERROR("Invalid {}: {}. Must be positive (> 0)", field_name, value);
        return false;
    }
    return true;
}

bool ConfigManager::ValidateLogLevel(const std::string& level) {
    static const std::vector<std::string> valid_levels = {
        "trace", "debug", "info", "warn", "error"
    };
    if (std::find(valid_levels.begin(), valid_levels.end(), level) == valid_levels.end()) {
        LOG_ERROR("Invalid logging.level: {}. Must be one of: trace, debug, info, warn, error", level);
        return false;
    }
    return true;
}

} // namespace minisearchrec
