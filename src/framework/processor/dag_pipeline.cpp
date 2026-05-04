// ============================================================
// MiniSearchRec - DAG 并行召回执行器实现
// 参考 Spark DAG 调度：事件驱动，节点完成后立即触发下游
// ============================================================

#include "framework/processor/dag_pipeline.h"
#include "framework/processor/dag_thread_pool.h"
#include "utils/logger.h"
#include <chrono>
#include <queue>

namespace minisearchrec {
namespace framework {

// ============================================================
// LoadFromConfig
// ============================================================
bool DagPipeline::LoadFromConfig(const YAML::Node& config,
                                  const std::string& yaml_key) {
    node_configs_.clear();
    processors_.clear();
    name_to_idx_.clear();
    downstream_.clear();

    if (!config || !config[yaml_key]) {
        loaded_ = true;
        return true;
    }

    const auto& stages = config[yaml_key];
    if (!stages.IsSequence()) {
        LOG_ERROR("DagPipeline::LoadFromConfig: '{}' is not a sequence", yaml_key);
        return false;
    }

    for (const auto& node : stages) {
        DagNodeConfig cfg;
        cfg.config.name = node["name"].as<std::string>("");
        cfg.config.weight = node["weight"].as<float>(1.0f);
        cfg.config.enable = node["enable"].as<bool>(true);
        cfg.config.params = node["params"] ? node["params"] : YAML::Node(YAML::NodeType::Map);

        if (cfg.config.name.empty()) {
            LOG_WARN("DagPipeline: empty processor name in '{}', skip", yaml_key);
            continue;
        }

        // 解析 depends_on
        if (node["depends_on"] && node["depends_on"].IsSequence()) {
            for (const auto& dep : node["depends_on"]) {
                cfg.depends_on.push_back(dep.as<std::string>());
            }
        }

        if (!cfg.config.enable) {
            LOG_INFO("DagPipeline: processor '{}' disabled, skip", cfg.config.name);
            node_configs_.push_back(cfg);
            processors_.push_back(nullptr);
            continue;
        }

        auto proc = ProcessorRegistry::Instance().Create(cfg.config.name);
        if (!proc) {
            LOG_WARN("DagPipeline: processor '{}' not registered "
                     "(forgot REGISTER_MSR_PROCESSOR?), skip", cfg.config.name);
            node_configs_.push_back(cfg);
            processors_.push_back(nullptr);
            continue;
        }

        int ret = proc->Init(cfg.config.params);
        if (ret != 0) {
            LOG_WARN("DagPipeline: processor '{}' Init failed (ret={}), skip",
                     cfg.config.name, ret);
            node_configs_.push_back(cfg);
            processors_.push_back(nullptr);
            continue;
        }

        proc->SetWeight(cfg.config.weight);
        proc->SetEnabled(cfg.config.enable);

        LOG_INFO("DagPipeline: processor '{}' ready (weight={:.2f}, deps={})",
                 cfg.config.name, cfg.config.weight, cfg.depends_on.size());

        node_configs_.push_back(cfg);
        processors_.push_back(std::move(proc));
    }

    if (!BuildDag()) {
        LOG_ERROR("DagPipeline: DAG build failed (cycle detected?)");
        return false;
    }

    loaded_ = true;
    LOG_INFO("DagPipeline: loaded {} nodes from '{}'", processors_.size(), yaml_key);
    return true;
}

// ============================================================
// BuildDag：拓扑验证 + 构建下游邻接表
// ============================================================
bool DagPipeline::BuildDag() {
    const size_t N = node_configs_.size();
    downstream_.resize(N);
    name_to_idx_.clear();

    // name → index 映射
    for (size_t i = 0; i < N; ++i) {
        name_to_idx_[node_configs_[i].config.name] = i;
    }

    // 验证依赖 + 构建下游邻接表
    for (size_t i = 0; i < N; ++i) {
        for (const auto& dep : node_configs_[i].depends_on) {
            auto it = name_to_idx_.find(dep);
            if (it == name_to_idx_.end()) {
                LOG_ERROR("DagPipeline: node '{}' depends on unknown '{}'",
                         node_configs_[i].config.name, dep);
                return false;
            }
            downstream_[it->second].push_back(i);
        }
    }

    // 环检测（Kahn's algorithm）
    std::vector<int> in_degree(N, 0);
    for (size_t i = 0; i < N; ++i) {
        in_degree[i] = static_cast<int>(node_configs_[i].depends_on.size());
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < N; ++i) {
        if (in_degree[i] == 0) q.push(i);
    }

    size_t visited = 0;
    while (!q.empty()) {
        size_t idx = q.front();
        q.pop();
        ++visited;
        for (size_t down : downstream_[idx]) {
            if (--in_degree[down] == 0) {
                q.push(down);
            }
        }
    }

    if (visited != N) {
        LOG_ERROR("DagPipeline: cycle detected in DAG (visited={}, total={})", visited, N);
        return false;
    }

    return true;
}

// ============================================================
// Execute：事件驱动 DAG 执行
// Spark 式调度：节点完成后立即触发下游，不等整层
// ============================================================
int DagPipeline::Execute(const Session* session,
                          std::vector<RecallOutputPtr>& outputs) const {
    const size_t N = processors_.size();
    if (N == 0) return 0;

    auto state = std::make_shared<DagExecState>();
    state->total = N;
    state->pending_deps = std::make_unique<std::atomic<int>[]>(N);
    state->outputs.resize(N);

    // 初始化每个节点的状态
    for (size_t i = 0; i < N; ++i) {
        state->pending_deps[i].store(
            static_cast<int>(node_configs_[i].depends_on.size()));
        state->outputs[i] = std::make_shared<RecallOutput>();
        state->outputs[i]->processor_name = node_configs_[i].config.name;
        state->outputs[i]->weight = node_configs_[i].config.weight;
    }

    // 提交所有根节点（无依赖）到线程池
    for (size_t i = 0; i < N; ++i) {
        if (state->pending_deps[i].load() == 0) {
            DagThreadPool::Instance().Submit(
                [this, i, session, state]() {
                    ExecuteNode(i, session, state);
                });
        }
    }

    // 等待所有节点完成
    {
        std::unique_lock<std::mutex> lk(state->mu);
        state->cv.wait(lk, [&]() { return state->completed.load() == N; });
    }

    // 收集输出（仅启用的、有结果的节点）
    for (size_t i = 0; i < N; ++i) {
        if (processors_[i] && state->outputs[i]->item_count > 0) {
            outputs.push_back(state->outputs[i]);
        }
    }

    LOG_INFO("DagPipeline::Execute: completed, outputs={}", outputs.size());
    return 0;
}

// ============================================================
// ExecuteNode：执行单个 DAG 节点
// 完成后原子递减下游节点 pending_count，归零则立即提交
// ============================================================
void DagPipeline::ExecuteNode(size_t idx,
                               const Session* session,
                               const std::shared_ptr<DagExecState>& state) const {
    auto& proc = processors_[idx];

    if (!proc || !proc->IsEnabled()) {
        goto advance;
    }

    // 超时检查
    if (session->IsTimedOut()) {
        LOG_WARN("DagPipeline: skipping '{}' (session timed out)", proc->Name());
        goto advance;
    }

    {
        auto t0 = std::chrono::steady_clock::now();

        // 构建上游输出（仅包含 depends_on 的算子）
        std::unordered_map<std::string, RecallOutputPtr> upstream;
        for (const auto& dep_name : node_configs_[idx].depends_on) {
            auto dep_it = name_to_idx_.find(dep_name);
            if (dep_it != name_to_idx_.end()) {
                upstream[dep_name] = state->outputs[dep_it->second];
            }
        }

        DagProcessorContext ctx{session, upstream, state->outputs[idx].get()};
        int ret = -1;
        try {
            ret = proc->ProcessDag(&ctx);
        } catch (const std::exception& e) {
            LOG_ERROR("DagPipeline: processor '{}' threw exception: {}", proc->Name(), e.what());
            ret = -1;
        } catch (...) {
            LOG_ERROR("DagPipeline: processor '{}' threw unknown exception", proc->Name());
            ret = -1;
        }

        auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();

        if (ret != 0) {
            LOG_WARN("DagPipeline: processor '{}' failed (ret={}), cost={}us",
                     proc->Name(), ret, cost_us);
        } else {
            LOG_DEBUG("DagPipeline: processor '{}' ok, items={}, cost={}us",
                      proc->Name(), state->outputs[idx]->item_count, cost_us);
        }
    }

advance:
    // 触发下游：原子递减 pending_count，归零则立即提交
    for (size_t down_idx : downstream_[idx]) {
        int prev = state->pending_deps[down_idx].fetch_sub(1);
        if (prev == 1) {
            // 所有依赖满足 → 立即调度（Spark 事件驱动核心）
            DagThreadPool::Instance().Submit(
                [this, down_idx, session, state]() {
                    ExecuteNode(down_idx, session, state);
                });
        }
    }

    // 检查是否全部完成
    if (state->completed.fetch_add(1) + 1 == state->total) {
        std::lock_guard<std::mutex> lk(state->mu);
        state->cv.notify_one();
    }
}

}  // namespace framework
}  // namespace minisearchrec
