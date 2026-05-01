// ============================================================
// MiniSearchRec - Pipeline 编排器
// 参考：微信搜推的 DAG 执行引擎（简化版）
// 作用：按配置顺序执行召回 → 粗排 → 精排 → 过滤 → 后处理
// ============================================================

#ifndef MINISEARCHREC_PIPELINE_H
#define MINISEARCHREC_PIPELINE_H

#include <vector>
#include <string>
#include <yaml-cpp/yaml.h>
#include "session.h"
#include "processor.h"

namespace minisearchrec {

// ============================================================
// Pipeline 配置结构体
// 从 YAML 配置文件加载
// ============================================================
struct PipelineConfig {
    // 召回阶段配置
    struct RecallStage {
        std::string name;           // 处理器名称
        bool enable = true;          // 是否启用
        int max_recall = 1000;      // 最大召回数量
        YAML::Node params;          // 额外参数
    };

    // 粗排阶段配置
    struct CoarseRankStage {
        std::string name;           // 打分器名称
        float weight = 1.0f;       // 权重
        YAML::Node params;
    };

    // 精排阶段配置
    struct FineRankStage {
        std::string name;
        float weight = 1.0f;
        std::string model_path;
        YAML::Node params;
    };

    // 过滤阶段配置
    struct FilterStage {
        std::string name;
        YAML::Node params;
    };

    // 后处理阶段配置
    struct PostProcessStage {
        std::string name;
        YAML::Node params;
    };

    std::vector<RecallStage> recall_stages;
    std::vector<CoarseRankStage> coarse_rank_stages;
    std::vector<FineRankStage> fine_rank_stages;
    std::vector<FilterStage> filter_stages;
    std::vector<PostProcessStage> postprocess_stages;

    // 全局配置
    int final_result_count = 20;    // 最终返回结果数
    bool enable_cache = true;        // 是否启用缓存
};

// ============================================================
// Pipeline 编排器
// 对应微信搜推的 DAG 执行引擎（简化版）
// ============================================================
class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    // 从 YAML 加载 Pipeline 配置
    bool LoadConfig(const std::string& config_path);

    // 从已加载的 YAML::Node 初始化（由 ConfigManager 提供，避免重复读文件）
    bool LoadFromNodes(const YAML::Node& recall_cfg,
                       const YAML::Node& rank_cfg,
                       const YAML::Node& filter_cfg);

    // 是否已初始化
    bool IsLoaded() const { return loaded_; }

    // 执行完整的搜索 Pipeline
    // 对应微信搜推的 DAG 执行
    int Execute(Session& session);

private:
    // 各阶段执行函数
    int ExecuteRecall(Session& session);
    int ExecuteCoarseRank(Session& session);
    int ExecuteFineRank(Session& session);
    int ExecuteFilter(Session& session);
    int ExecutePostProcess(Session& session);

    // 将 DocCandidate 转换为 SearchResult（响应组装）
    void BuildResponse(Session& session);

    PipelineConfig config_;
    YAML::Node raw_config_;
    bool loaded_ = false;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_PIPELINE_H
