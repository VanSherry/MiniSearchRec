// ============================================================
// MiniSearchRec - 排序处理器接口
// 对标：通用搜索框架 ProcessorInterface
// 每个 Processor 实现一个排序/过滤/特征步骤
// 通过 YAML 配置串联成 Pipeline
// ============================================================

#ifndef MINISEARCHREC_PROCESSOR_INTERFACE_H
#define MINISEARCHREC_PROCESSOR_INTERFACE_H

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include "lib/rank/base/rank_context.h"

namespace minisearchrec {
namespace rank {

// ============================================================
// ProcessorConfig：从 YAML 加载的 Processor 配置
// ============================================================
struct ProcessorConfig {
    std::string name;
    std::string json_params;  // 可选的 JSON 参数
};

// ============================================================
// ProcessorInterface：排序处理器接口
// ============================================================
class ProcessorInterface {
public:
    ProcessorInterface() = default;
    virtual ~ProcessorInterface() = default;

    // 初始化（每次请求调用，绑定上下文）
    virtual int Init(RankContextPtr ctx, const ProcessorConfig* config = nullptr) {
        ctx_ = std::move(ctx);
        config_ = config;
        return 0;
    }

    // 处理（核心逻辑）
    virtual int Process() = 0;

    // 名称（用于日志）
    virtual std::string Name() const { return "ProcessorInterface"; }

protected:
    RankContextPtr ctx_;
    const ProcessorConfig* config_ = nullptr;
};

using ProcessorInterfacePtr = std::shared_ptr<ProcessorInterface>;
using ProcessorCreator = std::function<ProcessorInterface*()>;

// ============================================================
// ProcessorRegistry：Processor 工厂注册表（单例）
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
        if (it == creators_.end()) return nullptr;
        return it->second();
    }

    bool Has(const std::string& name) const {
        return creators_.find(name) != creators_.end();
    }

private:
    ProcessorRegistry() = default;
    std::unordered_map<std::string, ProcessorCreator> creators_;
};

// ============================================================
// 注册宏
// ============================================================
#define REGISTER_RANK_PROCESSOR(ClassName) \
    static bool _registered_##ClassName = []() { \
        ::minisearchrec::rank::ProcessorRegistry::Instance().Register( \
            #ClassName, []() -> ::minisearchrec::rank::ProcessorInterface* { \
                return new ClassName(); \
            }); \
        return true; \
    }()

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_PROCESSOR_INTERFACE_H
