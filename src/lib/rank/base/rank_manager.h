// ============================================================
// MiniSearchRec - 排序管理器
// 对标：通用搜索框架 RankManager
// 根据 business_type 路由到对应的 Factory 和 Processor 链
// 通过 YAML 配置驱动，支持运行时新增业务
// ============================================================

#ifndef MINISEARCHREC_RANK_MANAGER_H
#define MINISEARCHREC_RANK_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "lib/rank/base/rank_factory.h"
#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {
namespace rank {

// ============================================================
// BusinessConfig：一个业务的排序配置
// ============================================================
struct BusinessRankConfig {
    std::string business_type;        // "search" / "sug" / "hint" / "nav"
    std::string factory_name;         // "SearchFactory" / "SugFactory" / ...
    std::vector<ProcessorConfig> processors;
};

// ============================================================
// RankManager：排序配置管理单例
// ============================================================
class RankManager {
public:
    static RankManager& Instance() {
        static RankManager inst;
        return inst;
    }

    // 初始化：注册业务配置（在 main 中调用）
    void RegisterBusiness(const BusinessRankConfig& config);

    // 按 business_type 获取 Factory
    const RankFactory* GetFactory(const std::string& business_type) const;

    // 按 business_type 获取 Processor 配置列表
    std::vector<ProcessorConfig> GetProcessors(const std::string& business_type) const;

    // 是否已注册某个 business_type
    bool HasBusiness(const std::string& business_type) const {
        return configs_.find(business_type) != configs_.end();
    }

private:
    RankManager() = default;

    // business_type → config
    std::unordered_map<std::string, BusinessRankConfig> configs_;
};

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_MANAGER_H
