// ============================================================
// MiniSearchRec - 框架层 Processor 接口
// 对标：通用搜索框架 ProcessorInterface
//
// 设计思想：
//   1. 统一 Processor 基类（召回/打分/过滤/后处理全部用同一基类）
//   2. 配置驱动：YAML 中声明 processor 列表，框架自动反射创建
//   3. 注册宏：业务 .cpp 末尾 REGISTER_MSR_PROCESSOR(MyProcessor)
//   4. Init 在服务启动时调用一次（加载模型等重操作）
//   5. Process 每次请求调用（轻量计算）
//
// 业务只需：
//   1. 继承 ProcessorInterface，实现 Init + Process
//   2. 在 .cpp 末尾 REGISTER_MSR_PROCESSOR(ClassName)
//   3. 在业务 YAML 中声明 processor 名字
// ============================================================

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "framework/session/session.h"

namespace minisearchrec {
namespace framework {

// ============================================================
// ProcessorConfig：从 YAML 加载的单个 Processor 配置
// ============================================================
struct ProcessorConfig {
    std::string name;           // Processor 类名（用于反射）
    float weight = 1.0f;       // 权重（排序融合用）
    bool enable = true;        // 是否启用
    YAML::Node params;         // 自定义参数（传给 Init）
};

// ============================================================
// ProcessorInterface：统一 Processor 基类
// 对标通用搜索框架 ProcessorInterface
// ============================================================
class ProcessorInterface {
public:
    ProcessorInterface() = default;
    virtual ~ProcessorInterface() = default;

    // 初始化（服务启动时调用一次，加载模型/配置等重操作）
    // config: YAML 参数节点
    virtual int Init(const YAML::Node& config) { return 0; }

    // 处理（每次请求调用）
    // session: 当前请求的 Session（基类指针，业务内部 dynamic_cast）
    virtual int Process(Session* session) = 0;

    // 名称（用于日志/监控）
    virtual std::string Name() const = 0;

    // 是否启用
    virtual bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }

    // 权重（排序融合用）
    float GetWeight() const { return weight_; }
    void SetWeight(float w) { weight_ = w; }

protected:
    bool enabled_ = true;
    float weight_ = 1.0f;
};

using ProcessorPtr = std::shared_ptr<ProcessorInterface>;
using ProcessorCreator = std::function<ProcessorInterface*()>;

// ============================================================
// ProcessorRegistry：Processor 注册表（单例）
// 对标通用反射注册框架
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

    ProcessorPtr Create(const std::string& name) const {
        auto it = creators_.find(name);
        if (it == creators_.end()) return nullptr;
        return ProcessorPtr(it->second());
    }

    bool Has(const std::string& name) const {
        return creators_.find(name) != creators_.end();
    }

    std::vector<std::string> ListAll() const {
        std::vector<std::string> names;
        for (const auto& [k, v] : creators_) names.push_back(k);
        return names;
    }

private:
    ProcessorRegistry() = default;
    std::unordered_map<std::string, ProcessorCreator> creators_;
};

// ============================================================
// 注册宏
// 用法：在 .cpp 末尾 REGISTER_MSR_PROCESSOR(BM25ScorerProcessor)
// ============================================================
#define REGISTER_MSR_PROCESSOR(ClassName)                                    \
    namespace {                                                              \
    struct ClassName##_ProcRegistrar {                                       \
        ClassName##_ProcRegistrar() {                                        \
            ::minisearchrec::framework::ProcessorRegistry::Instance()        \
                .Register(#ClassName,                                        \
                    []() -> ::minisearchrec::framework::ProcessorInterface* {\
                        return new ClassName();                              \
                    });                                                      \
        }                                                                    \
    };                                                                       \
    static ClassName##_ProcRegistrar g_##ClassName##_proc_registrar;         \
    }

} // namespace framework
} // namespace minisearchrec
