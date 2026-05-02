// ============================================================
// MiniSearchRec - 框架层 Processor Pipeline
// 对标：通用搜索框架 Rank::DoRank 中的 Processor 链调度
//
// 设计：
//   1. 按阶段（recall/rank/rerank/filter/postprocess）分别持有 Processor 链
//   2. 服务启动时从 YAML 配置读取各阶段 processor 列表
//   3. 反射创建 + Init（一次性，避免每请求 new + LoadModel）
//   4. 每次请求按序调用 Process(session)
//   5. 支持热更新（HotReload）
//
// 与 BaseHandler 的关系：
//   - BaseHandler::DoSearch → 调用 recall_pipeline
//   - BaseHandler::DoRank   → 调用 rank_pipeline
//   - BaseHandler::DoRerank → 调用 rerank_pipeline
//   - BaseHandler::DoInterpose → 调用 filter_pipeline + postprocess_pipeline
//
// 业务如需自定义：override DoSearch/DoRank 等，不走默认 Pipeline 即可
// ============================================================

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "framework/processor/processor_interface.h"

namespace minisearchrec {
namespace framework {

// ============================================================
// ProcessorPipeline：一条 Processor 链
// ============================================================
class ProcessorPipeline {
public:
    ProcessorPipeline() = default;
    ~ProcessorPipeline() = default;

    // 从 YAML 配置加载 Processor 列表
    // yaml_key: 在配置中的 key（如 "recall_stages"、"coarse_rank_stages"）
    bool LoadFromConfig(const YAML::Node& config, const std::string& yaml_key);

    // 执行所有 Processor
    int Execute(Session* session) const;

    // 获取 Processor 数量
    size_t Size() const { return processors_.size(); }

    // 获取所有 Processor（用于热更新等）
    const std::vector<ProcessorPtr>& GetProcessors() const { return processors_; }
    std::vector<ProcessorPtr>& GetProcessors() { return processors_; }

    // 按名称获取 Processor
    ProcessorPtr GetByName(const std::string& name) const;

    bool IsLoaded() const { return loaded_; }

private:
    std::vector<ProcessorPtr> processors_;
    std::vector<ProcessorConfig> configs_;
    bool loaded_ = false;
};

// ============================================================
// PipelineManager：管理所有业务的 Pipeline 配置
// 按 business_type 路由到各自的阶段 Pipeline
// ============================================================
struct BusinessPipelineConfig {
    std::string business_type;
    ProcessorPipeline recall_pipeline;
    ProcessorPipeline rank_pipeline;       // 粗排
    ProcessorPipeline rerank_pipeline;     // 精排
    ProcessorPipeline filter_pipeline;     // 过滤
    ProcessorPipeline postprocess_pipeline; // 后处理
};

class PipelineManager {
public:
    static PipelineManager& Instance() {
        static PipelineManager inst;
        return inst;
    }

    // 初始化：从 YAML 配置目录加载所有业务的 Pipeline 配置
    bool Init(const std::string& config_dir);

    // 按 business_type 获取 Pipeline 配置
    BusinessPipelineConfig* GetConfig(const std::string& business_type);
    const BusinessPipelineConfig* GetConfig(const std::string& business_type) const;

    // 热更新某个阶段的 Processor
    bool HotReload(const std::string& business_type, const std::string& stage,
                   const YAML::Node& new_config);

private:
    PipelineManager() = default;
    std::unordered_map<std::string, BusinessPipelineConfig> configs_;
    mutable std::mutex mu_;
};

} // namespace framework
} // namespace minisearchrec
