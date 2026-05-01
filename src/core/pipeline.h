// ============================================================
// MiniSearchRec - Pipeline 编排器
// 参考：微信搜推的 DAG 执行引擎（简化版）
// 作用：按配置顺序执行召回 → 粗排 → 精排 → 过滤 → 后处理
// ============================================================

#ifndef MINISEARCHREC_PIPELINE_H
#define MINISEARCHREC_PIPELINE_H

#include <vector>
#include <string>
#include <memory>
#include <yaml-cpp/yaml.h>
#include "session.h"
#include "processor.h"

namespace minisearchrec {

// ============================================================
// Pipeline 配置结构体
// 从 YAML 配置文件加载
// ============================================================
struct PipelineConfig {
    struct RecallStage {
        std::string name;
        bool enable = true;
        int max_recall = 1000;
        YAML::Node params;
    };
    struct CoarseRankStage {
        std::string name;
        float weight = 1.0f;
        YAML::Node params;
    };
    struct FineRankStage {
        std::string name;
        float weight = 1.0f;
        std::string model_path;
        YAML::Node params;
    };
    struct FilterStage {
        std::string name;
        YAML::Node params;
    };
    struct PostProcessStage {
        std::string name;
        YAML::Node params;
    };

    std::vector<RecallStage>       recall_stages;
    std::vector<CoarseRankStage>   coarse_rank_stages;
    std::vector<FineRankStage>     fine_rank_stages;
    std::vector<FilterStage>       filter_stages;
    std::vector<PostProcessStage>  postprocess_stages;

    int  final_result_count = 20;
    bool enable_cache       = true;
};

// ============================================================
// Pipeline 编排器
// Scorer / Filter / PostProcess 实例在 LoadFromNodes 时创建一次，
// 避免每次请求重复 new + LoadModel。
// ============================================================
class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    bool LoadConfig(const std::string& config_path);
    bool LoadFromNodes(const YAML::Node& recall_cfg,
                       const YAML::Node& rank_cfg,
                       const YAML::Node& filter_cfg);

    bool IsLoaded() const { return loaded_; }

    int Execute(Session& session);

    // A/B：允许外部在 session 中注入实验参数覆盖，
    // Pipeline 读取 session.ab_params 动态调整行为
    struct ABParams {
        float mmr_lambda      = -1.f;  // <0 表示不覆盖
        int   coarse_top_k    = -1;
        int   fine_top_k      = -1;
    };

private:
    int ExecuteRecall(Session& session);
    int ExecuteCoarseRank(Session& session);
    int ExecuteFineRank(Session& session);
    int ExecuteFilter(Session& session);
    int ExecutePostProcess(Session& session);
    void BuildResponse(Session& session);

    PipelineConfig config_;
    YAML::Node     raw_config_;
    bool           loaded_ = false;

    // 预初始化的 processor 实例（生命周期与 Pipeline 相同）
    std::vector<std::shared_ptr<BaseScorerProcessor>>      coarse_scorers_;
    std::vector<std::shared_ptr<BaseScorerProcessor>>      fine_scorers_;
    std::vector<std::shared_ptr<BaseFilterProcessor>>      filters_;
    std::vector<std::shared_ptr<BasePostProcessProcessor>> postprocessors_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_PIPELINE_H
