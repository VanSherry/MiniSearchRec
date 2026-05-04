// ============================================================
// MiniSearchRec - DAG 并行召回执行器
// 参考 Spark DAG 调度：事件驱动，节点完成后立即触发下游
//
// 设计：
//   1. YAML 配置 depends_on 声明依赖关系
//   2. 拓扑排序验证无环
//   3. 事件驱动执行：节点完成后原子递减下游 pending_count，
//      归零则立即提交线程池（不等同层其他节点）
//   4. Session 在 DAG 执行期间 const 不可修改
//   5. 每个算子有独立 RecallOutput，写入无竞争
//   6. 全部完成后由 MergeRecall 统一合并到 Session
// ============================================================

#pragma once

#include <any>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "framework/processor/processor_interface.h"
#include "framework/session/session.h"

namespace minisearchrec {
namespace framework {

// ============================================================
// RecallOutput：单个算子的召回输出（DAG 并行模式）
// ============================================================
struct RecallOutput {
    std::string processor_name;
    float weight = 1.0f;
    size_t item_count = 0;

    // 轻量摘要（框架层可直接访问，用于日志/监控）
    std::vector<std::pair<std::string, float>> doc_scores;  // (doc_id, score)

    // 业务全量数据（类型由业务定义，如 std::vector<DocCandidate>）
    // MergeRecall 中通过 std::any_cast 获取
    std::any items;

    // 扩展元数据
    std::unordered_map<std::string, std::string> meta;
};

using RecallOutputPtr = std::shared_ptr<RecallOutput>;

// ============================================================
// DagProcessorContext：DAG 算子上下文（per-processor, per-request）
//
// - session: 只读，DAG 执行期间不可修改
// - upstream_outputs: 上游算子输出（只读）
// - output: 本算子输出（可写，算子独占，无竞争）
// ============================================================
struct DagProcessorContext {
    const Session* session;
    const std::unordered_map<std::string, RecallOutputPtr>& upstream_outputs;
    RecallOutput* output;
};

// ============================================================
// DagNodeConfig：DAG 节点配置
// ============================================================
struct DagNodeConfig {
    ProcessorConfig config;
    std::vector<std::string> depends_on;  // 依赖的上游算子名列表
};

// ============================================================
// DagExecState：DAG 执行期的共享状态（per-request）
// ============================================================
struct DagExecState {
    std::unique_ptr<std::atomic<int>[]> pending_deps;  // 每个节点剩余依赖数
    std::vector<RecallOutputPtr> outputs;               // 每个节点的输出
    std::atomic<size_t> completed{0};                   // 已完成节点数
    std::mutex mu;
    std::condition_variable cv;
    size_t total = 0;
};

// ============================================================
// DagPipeline：事件驱动 DAG 并行执行器
// 参考 Spark DAG 调度
// ============================================================
class DagPipeline {
public:
    DagPipeline() = default;
    ~DagPipeline() = default;

    // 从 YAML 配置加载 DAG 节点
    bool LoadFromConfig(const YAML::Node& config, const std::string& yaml_key);

    // 事件驱动并行执行 DAG，返回每个算子的输出
    int Execute(const Session* session,
                std::vector<RecallOutputPtr>& outputs) const;

    size_t Size() const { return processors_.size(); }
    bool IsLoaded() const { return loaded_; }

private:
    // 执行单个节点（由线程池回调）
    void ExecuteNode(size_t idx,
                     const Session* session,
                     const std::shared_ptr<DagExecState>& state) const;

    // 拓扑验证 + 构建下游邻接表
    bool BuildDag();

    std::vector<DagNodeConfig> node_configs_;
    std::vector<ProcessorPtr> processors_;
    std::unordered_map<std::string, size_t> name_to_idx_;  // name → node index
    std::vector<std::vector<size_t>> downstream_;           // node → downstream nodes
    bool loaded_ = false;
};

}  // namespace framework
}  // namespace minisearchrec
