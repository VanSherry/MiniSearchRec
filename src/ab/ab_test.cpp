// ============================================================
// MiniSearchRec - A/B 实验框架实现
// 参考：业界 AB 实验框架
// ============================================================

#include "ab/ab_test.h"
#include <cmath>
#include <random>
#include <iostream>

namespace minisearchrec {

namespace {

uint32_t SimpleHash(const std::string& s) {
    uint32_t hash = 5381;
    for (char c : s) {
        hash = ((hash << 5) + hash) + static_cast<uint32_t>(c);
    }
    return hash;
}

} // anonymous namespace

bool ABTestManager::LoadFromYAML(const YAML::Node& config) {
    if (!config.IsSequence()) return false;

    experiments_.clear();
    for (const auto& node : config) {
        ExperimentConfig exp;
        exp.name = node["name"].as<std::string>("unknown");
        exp.traffic_ratio = node["traffic_ratio"].as<float>(0.0f);
        exp.bucket_method = node["bucket_method"].as<std::string>("hash");

        if (node["params"]) {
            for (const auto& param : node["params"]) {
                std::string key = param["key"].as<std::string>();
                std::string val = param["value"].as<std::string>();
                exp.params[key] = val;
            }
        }

        experiments_.push_back(exp);
    }

    std::cout << "[ABTestManager] Loaded " << experiments_.size()
              << " experiments.\n";
    return true;
}

bool ABTestManager::LoadFromFile(const std::string& path) {
    try {
        YAML::Node config = YAML::LoadFile(path);
        return LoadFromYAML(config);
    } catch (const YAML::Exception& e) {
        std::cerr << "[ABTestManager] Failed to load " << path
                  << ": " << e.what() << "\n";
        return false;
    }
}

const ExperimentConfig* ABTestManager::AssignExperiment(
    const std::string& uid
) const {
    if (uid.empty() || experiments_.empty()) {
        return nullptr;
    }

    uint32_t hash_val = ConsistentHash(uid);
    uint32_t bucket = hash_val % 100;  // 100 个桶

    float cumulative = 0.0f;
    for (const auto& exp : experiments_) {
        cumulative += exp.traffic_ratio * 100.0f;
        if (bucket < static_cast<uint32_t>(cumulative)) {
            return &exp;
        }
    }

    return nullptr;  // 对照组
}

std::string ABTestManager::GetParam(
    const std::string& uid,
    const std::string& key,
    const std::string& default_value
) const {
    const auto* exp = AssignExperiment(uid);
    if (exp) {
        auto it = exp->params.find(key);
        if (it != exp->params.end()) {
            return it->second;
        }
    }
    return default_value;
}

std::vector<std::string> ABTestManager::ListExperiments() const {
    std::vector<std::string> names;
    for (const auto& exp : experiments_) {
        names.push_back(exp.name);
    }
    return names;
}

uint32_t ABTestManager::ConsistentHash(const std::string& uid) const {
    return SimpleHash(uid);
}

} // namespace minisearchrec
