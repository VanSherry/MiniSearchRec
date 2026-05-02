// ============================================================
// MiniSearchRec - HandlerManager
// 对标：通用搜索框架 HandlerManager
// 管理所有 Handler 单例，通过 business_type 路由到对应 Handler
// 配置驱动：读取 BusinessConfig 列表，反射创建 Handler 并初始化
// ============================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "framework/handler/base_handler.h"

namespace minisearchrec {
namespace framework {

class HandlerManager {
public:
    static HandlerManager& Instance() {
        static HandlerManager mgr;
        return mgr;
    }

    // 配置驱动初始化（推荐）：从 YAML 文件读取 business 列表，反射创建 Handler
    // 对标通用搜索框架 HandlerManager::Init
    int32_t InitFromConfig(const std::string& config_path);

    // 从已解析的配置列表初始化
    int32_t Init(const std::vector<BusinessConfig>& configs);

    // 手动注册一个 Handler（编程式注册，不通过配置文件）
    int32_t RegisterHandler(const BusinessConfig& config, BaseHandler* handler);

    // 通过 business_type 获取 Handler
    const BaseHandler* GetHandler(const std::string& business_type) const;
    const BaseHandler* GetHandler(uint64_t business_type) const;

    // 获取所有已注册的 business_type
    std::vector<std::string> GetAllBusinessTypes() const;

    // 获取所有配置
    const std::vector<BusinessConfig>& GetConfigs() const { return configs_; }

private:
    HandlerManager() = default;

    // business_type(string) → Handler*
    std::unordered_map<std::string, BaseHandler*> handler_map_;

    // 保存配置（用于诊断/热更新）
    std::vector<BusinessConfig> configs_;
};

} // namespace framework
} // namespace minisearchrec
