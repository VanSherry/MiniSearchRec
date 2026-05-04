#ifndef MINISEARCHREC_RANK_ENGINE_H
#define MINISEARCHREC_RANK_ENGINE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace minisearchrec {
namespace rank {

// ============================================================
// RankItem：排序元素（纯数据，无业务依赖）
// ============================================================
struct RankItem {
    std::string id;                       // doc_id / word
    std::string source;                   // recall_source
    float score = 0.0f;                   // 最终排序分
    std::unordered_map<std::string, float> features;  // KV 特征

    void SetFeature(const std::string& k, float v) { features[k] = v; }
    float GetFeature(const std::string& k, float def = 0.0f) const {
        auto it = features.find(k);
        return it != features.end() ? it->second : def;
    }
};

// ============================================================
// ProcessorConfig / StageConfig：从 YAML 加载
// ============================================================
struct ProcessorConfig {
    std::string name;
    float weight = 1.0f;
    YAML::Node params;
};

struct StageConfig {
    std::vector<ProcessorConfig> processors;
    int top_k = -1;  // -1 = 不截断
};

// ============================================================
// BusinessRankConfig：一个业务的完整 Rank 配置
// ============================================================
struct BusinessRankConfig {
    StageConfig rank;
    StageConfig rerank;
};

// ============================================================
// ProcessorInterface：打分算子接口
// ============================================================
class ProcessorInterface {
public:
    virtual ~ProcessorInterface() = default;

    virtual int Init(const ProcessorConfig* config) {
        config_ = config;
        return 0;
    }
    virtual int Process(std::vector<RankItem>& items) = 0;
    virtual std::string Name() const { return "ProcessorInterface"; }

protected:
    const ProcessorConfig* config_ = nullptr;
    float weight_ = 1.0f;
};

using ProcessorCreator = std::function<ProcessorInterface*()>;

// ============================================================
// ProcessorRegistry：算子注册表
// ============================================================
class ProcessorRegistry {
public:
    static ProcessorRegistry& Instance() {
        static ProcessorRegistry inst;
        return inst;
    }

    void Register(const std::string& name, ProcessorCreator creator) {
        creators_[name] = std::move(creator);
    }

    ProcessorInterface* Create(const std::string& name) const {
        auto it = creators_.find(name);
        return it != creators_.end() ? it->second() : nullptr;
    }

    bool Has(const std::string& name) const {
        return creators_.find(name) != creators_.end();
    }

private:
    ProcessorRegistry() = default;
    std::unordered_map<std::string, ProcessorCreator> creators_;
};

#define REGISTER_RANK_PROCESSOR(ClassName) \
    static bool _registered_##ClassName = []() { \
        ::minisearchrec::rank::ProcessorRegistry::Instance().Register( \
            #ClassName, []() -> ::minisearchrec::rank::ProcessorInterface* { \
                return new ClassName(); \
            }); \
        return true; \
    }()

// ============================================================
// RankEngine：纯计算引擎
// 输入 items，按 stage 配置执行 Processor 链
// items 原地更新 score 字段
// ============================================================
class RankEngine {
public:
    // 在 items 上执行指定 processors，原地修改
    static void Score(std::vector<RankItem>& items,
                      const std::vector<ProcessorConfig>& processor_configs);
};

// ============================================================
// RankConfigManager：YAML 配置管理
// ============================================================
class RankConfigManager {
public:
    static RankConfigManager& Instance() {
        static RankConfigManager inst;
        return inst;
    }

    bool LoadFromConfig(const YAML::Node& yaml, const std::string& business_type);
    const StageConfig* GetStageConfig(const std::string& business_type,
                                       const std::string& stage) const;

private:
    RankConfigManager() = default;

    struct BusinessConfig {
        StageConfig rank;
        StageConfig rerank;
    };
    std::unordered_map<std::string, BusinessConfig> configs_;
};

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_ENGINE_H
