// ============================================================
// MiniSearchRec - 处理器基类与接口定义
// 参考：mmsearchqxcommon 的 Processor Chain 和 Factory Pattern
// 设计思想：插件化、配置驱动、可热插拔
// ============================================================

#ifndef MINISEARCHREC_PROCESSOR_H
#define MINISEARCHREC_PROCESSOR_H

#include <string>
#include <memory>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "session.h"

namespace minisearchrec {

// ============================================================
// 召回处理器基类
// 对应微信搜推的 RecallProcessor
// ============================================================
class BaseRecallProcessor {
public:
    virtual ~BaseRecallProcessor() = default;

    // 执行召回，结果写入 session.recall_results
    // 返回 0 表示成功，非 0 表示失败
    virtual int Process(Session& session) = 0;

    // 处理器名称（用于注册和配置引用）
    virtual std::string Name() const = 0;

    // 从 YAML 配置初始化
    virtual bool Init(const YAML::Node& config) = 0;

    // 是否启用（可从配置读取）
    virtual bool IsEnabled() const { return enabled_; }

protected:
    bool enabled_ = true;
};

// ============================================================
// 打分处理器基类（用于粗排/精排）
// 对应微信搜推的 ScorerProcessor
// ============================================================
class BaseScorerProcessor {
public:
    virtual ~BaseScorerProcessor() = default;

    // 对候选集中每个文档打分，更新 candidate 的对应分数字段
    virtual int Process(Session& session,
                        std::vector<DocCandidate>& candidates) = 0;

    virtual std::string Name() const = 0;
    virtual bool Init(const YAML::Node& config) = 0;

    // 打分权重（用于多打分器融合）
    virtual float Weight() const { return weight_; }

protected:
    float weight_ = 1.0f;
};

// ============================================================
// 过滤处理器基类
// 对应微信搜推的 FilterProcessor
// ============================================================
class BaseFilterProcessor {
public:
    virtual ~BaseFilterProcessor() = default;

    // 判断候选文档是否应该保留
    virtual bool ShouldKeep(const Session& session,
                            const DocCandidate& candidate) = 0;

    virtual std::string Name() const = 0;
    virtual bool Init(const YAML::Node& config) = 0;
};

// ============================================================
// 后处理处理器基类（重排、截断等）
// ============================================================
class BasePostProcessProcessor {
public:
    virtual ~BasePostProcessProcessor() = default;

    // 对最终结果进行后处理（多样性重排、分页等）
    virtual int Process(Session& session,
                        std::vector<DocCandidate>& candidates) = 0;

    virtual std::string Name() const = 0;
    virtual bool Init(const YAML::Node& config) = 0;
};

// ============================================================
// 处理器工厂（单例）
// 对应微信搜推的 ClassRegister 机制
// ============================================================
class ProcessorFactory {
public:
    static ProcessorFactory& Instance() {
        static ProcessorFactory inst;
        return inst;
    }

    // --- 召回处理器注册与创建 ---
    using RecallCreator = std::function<std::unique_ptr<BaseRecallProcessor>()>;

    void RegisterRecall(const std::string& name, RecallCreator creator) {
        recall_creators_[name] = std::move(creator);
    }

    std::unique_ptr<BaseRecallProcessor> CreateRecall(const std::string& name) {
        auto it = recall_creators_.find(name);
        if (it == recall_creators_.end()) return nullptr;
        return it->second();
    }

    std::vector<std::string> ListRecallProcessors() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : recall_creators_) {
            names.push_back(name);
        }
        return names;
    }

    // --- 打分处理器注册与创建 ---
    using ScorerCreator = std::function<std::unique_ptr<BaseScorerProcessor>()>;

    void RegisterScorer(const std::string& name, ScorerCreator creator) {
        scorer_creators_[name] = std::move(creator);
    }

    std::unique_ptr<BaseScorerProcessor> CreateScorer(const std::string& name) {
        auto it = scorer_creators_.find(name);
        if (it == scorer_creators_.end()) return nullptr;
        return it->second();
    }

    // --- 过滤处理器注册与创建 ---
    using FilterCreator = std::function<std::unique_ptr<BaseFilterProcessor>()>;

    void RegisterFilter(const std::string& name, FilterCreator creator) {
        filter_creators_[name] = std::move(creator);
    }

    std::unique_ptr<BaseFilterProcessor> CreateFilter(const std::string& name) {
        auto it = filter_creators_.find(name);
        if (it == filter_creators_.end()) return nullptr;
        return it->second();
    }

    // --- 后处理处理器注册与创建 ---
    using PostProcessCreator = std::function<std::unique_ptr<BasePostProcessProcessor>()>;

    void RegisterPostProcess(const std::string& name, PostProcessCreator creator) {
        postprocess_creators_[name] = std::move(creator);
    }

    std::unique_ptr<BasePostProcessProcessor> CreatePostProcess(const std::string& name) {
        auto it = postprocess_creators_.find(name);
        if (it == postprocess_creators_.end()) return nullptr;
        return it->second();
    }

private:
    ProcessorFactory() = default;

    std::map<std::string, RecallCreator> recall_creators_;
    std::map<std::string, ScorerCreator> scorer_creators_;
    std::map<std::string, FilterCreator> filter_creators_;
    std::map<std::string, PostProcessCreator> postprocess_creators_;
};

// ============================================================
// 自动注册宏（简化写法，参考微信搜推 REGISTER_CLASS）
// 使用方式：在 .cpp 文件末尾调用 REGISTER_RECALL(MyRecallProcessor)
// ============================================================
#define REGISTER_RECALL(ClassName) \
    static const bool _recall_reg_##ClassName = []() { \
        ProcessorFactory::Instance().RegisterRecall( \
            #ClassName, \
            []() { return std::make_unique<ClassName>(); } \
        ); \
        return true; \
    }()

#define REGISTER_SCORER(ClassName) \
    static const bool _scorer_reg_##ClassName = []() { \
        ProcessorFactory::Instance().RegisterScorer( \
            #ClassName, \
            []() { return std::make_unique<ClassName>(); } \
        ); \
        return true; \
    }()

#define REGISTER_FILTER(ClassName) \
    static const bool _filter_reg_##ClassName = []() { \
        ProcessorFactory::Instance().RegisterFilter( \
            #ClassName, \
            []() { return std::make_unique<ClassName>(); } \
        ); \
        return true; \
    }()

#define REGISTER_POSTPROCESS(ClassName) \
    static const bool _postproc_reg_##ClassName = []() { \
        ProcessorFactory::Instance().RegisterPostProcess( \
            #ClassName, \
            []() { return std::make_unique<ClassName>(); } \
        ); \
        return true; \
    }()

} // namespace minisearchrec

#endif // MINISEARCHREC_PROCESSOR_H
