// ============================================================
// MiniSearchRec - 框架层 Processor Pipeline 实现
// ============================================================

#include "framework/processor/processor_pipeline.h"
#include "utils/logger.h"
#include <chrono>
#include <filesystem>

namespace minisearchrec {
namespace framework {

// ============================================================
// ProcessorPipeline
// ============================================================

bool ProcessorPipeline::LoadFromConfig(const YAML::Node& config,
                                       const std::string& yaml_key) {
    processors_.clear();
    configs_.clear();

    if (!config || !config[yaml_key]) {
        // 没有该阶段的配置，不是错误（业务可能不需要某阶段）
        loaded_ = true;
        return true;
    }

    const auto& stages = config[yaml_key];
    if (!stages.IsSequence()) {
        LOG_ERROR("ProcessorPipeline::LoadFromConfig: '{}' is not a sequence", yaml_key);
        return false;
    }

    for (const auto& node : stages) {
        ProcessorConfig cfg;
        cfg.name = node["name"].as<std::string>("");
        cfg.weight = node["weight"].as<float>(1.0f);
        cfg.enable = node["enable"].as<bool>(true);
        cfg.params = node["params"];

        if (cfg.name.empty()) {
            LOG_WARN("ProcessorPipeline::LoadFromConfig: empty processor name in '{}', skip",
                     yaml_key);
            continue;
        }

        if (!cfg.enable) {
            LOG_INFO("ProcessorPipeline: processor '{}' disabled, skip", cfg.name);
            configs_.push_back(cfg);
            processors_.push_back(nullptr);
            continue;
        }

        // 反射创建 Processor
        auto proc = ProcessorRegistry::Instance().Create(cfg.name);
        if (!proc) {
            LOG_WARN("ProcessorPipeline: processor '{}' not registered "
                     "(forgot REGISTER_MSR_PROCESSOR?), skip", cfg.name);
            configs_.push_back(cfg);
            processors_.push_back(nullptr);
            continue;
        }

        // Init（一次性：加载模型等重操作）
        int ret = proc->Init(cfg.params);
        if (ret != 0) {
            LOG_WARN("ProcessorPipeline: processor '{}' Init failed (ret={}), skip",
                     cfg.name, ret);
            configs_.push_back(cfg);
            processors_.push_back(nullptr);
            continue;
        }

        proc->SetWeight(cfg.weight);
        proc->SetEnabled(cfg.enable);

        LOG_INFO("ProcessorPipeline: processor '{}' ready (weight={:.2f})",
                 cfg.name, cfg.weight);

        configs_.push_back(cfg);
        processors_.push_back(std::move(proc));
    }

    loaded_ = true;
    LOG_INFO("ProcessorPipeline: loaded {} processors from '{}'",
             processors_.size(), yaml_key);
    return true;
}

int ProcessorPipeline::Execute(Session* session) const {
    for (size_t i = 0; i < processors_.size(); ++i) {
        auto& proc = processors_[i];
        if (!proc || !proc->IsEnabled()) continue;

        auto t0 = std::chrono::steady_clock::now();
        int ret = proc->Process(session);
        auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();

        if (ret != 0) {
            LOG_WARN("ProcessorPipeline: processor '{}' failed (ret={}), cost={}us",
                     proc->Name(), ret, cost_us);
            // 继续执行下一个（非致命错误）
            continue;
        }

        LOG_DEBUG("ProcessorPipeline: processor '{}' ok, cost={}us",
                  proc->Name(), cost_us);

        // 超时检查
        if (session->IsTimedOut()) {
            LOG_WARN("ProcessorPipeline: timeout after processor '{}'", proc->Name());
            break;
        }
    }
    return 0;
}

ProcessorPtr ProcessorPipeline::GetByName(const std::string& name) const {
    for (auto& p : processors_) {
        if (p && p->Name() == name) return p;
    }
    return nullptr;
}

// ============================================================
// PipelineManager
// ============================================================

bool PipelineManager::Init(const std::string& config_dir) {
    std::lock_guard<std::mutex> lock(mu_);

    // 自动扫描 config/biz/ 目录下所有 .yaml 文件
    // 文件名即 business_type（如 search.yaml → business_type="search"）
    // 框架不知道有哪些业务，完全由配置决定
    std::string biz_dir = config_dir + "/biz";

    // 遍历 biz/ 目录（使用 C++17 filesystem）
    namespace fs = std::filesystem;
    if (!fs::exists(biz_dir) || !fs::is_directory(biz_dir)) {
        LOG_WARN("PipelineManager: biz config dir '{}' not found, no pipelines loaded", biz_dir);
        return true;  // 不是错误（业务可能不需要 Pipeline）
    }

    for (const auto& entry : fs::directory_iterator(biz_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".yaml" && entry.path().extension() != ".yml") continue;

        std::string business_type = entry.path().stem().string();  // search.yaml → "search"
        std::string file_path = entry.path().string();

        YAML::Node yaml;
        try {
            yaml = YAML::LoadFile(file_path);
        } catch (const YAML::Exception& e) {
            LOG_WARN("PipelineManager: failed to load '{}': {}", file_path, e.what());
            continue;
        }

        if (!yaml || yaml.IsNull()) continue;

        // 统一的 stage key 约定：
        //   recall_stages / coarse_rank_stages / fine_rank_stages / filter_stages / postprocess_stages
        // 或简化版：
        //   recall_stages / rank_stages / rerank_stages / filter_stages / postprocess_stages
        BusinessPipelineConfig cfg;
        cfg.business_type = business_type;

        cfg.recall_pipeline.LoadFromConfig(yaml, "recall_stages");

        // 粗排：优先 coarse_rank_stages，fallback 到 rank_stages
        if (yaml["coarse_rank_stages"]) {
            cfg.rank_pipeline.LoadFromConfig(yaml, "coarse_rank_stages");
        } else {
            cfg.rank_pipeline.LoadFromConfig(yaml, "rank_stages");
        }

        // 精排：优先 fine_rank_stages，fallback 到 rerank_stages
        if (yaml["fine_rank_stages"]) {
            cfg.rerank_pipeline.LoadFromConfig(yaml, "fine_rank_stages");
        } else {
            cfg.rerank_pipeline.LoadFromConfig(yaml, "rerank_stages");
        }

        cfg.filter_pipeline.LoadFromConfig(yaml, "filter_stages");
        cfg.postprocess_pipeline.LoadFromConfig(yaml, "postprocess_stages");

        configs_[business_type] = std::move(cfg);
        LOG_INFO("PipelineManager: '{}' pipeline loaded from '{}'", business_type, file_path);
    }

    LOG_INFO("PipelineManager: initialized with {} business types", configs_.size());
    return true;
}

BusinessPipelineConfig* PipelineManager::GetConfig(const std::string& business_type) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = configs_.find(business_type);
    if (it == configs_.end()) return nullptr;
    return &it->second;
}

const BusinessPipelineConfig* PipelineManager::GetConfig(
    const std::string& business_type) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = configs_.find(business_type);
    if (it == configs_.end()) return nullptr;
    return &it->second;
}

bool PipelineManager::HotReload(const std::string& business_type,
                                const std::string& stage,
                                const YAML::Node& new_config) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = configs_.find(business_type);
    if (it == configs_.end()) {
        LOG_ERROR("PipelineManager::HotReload: unknown business_type '{}'", business_type);
        return false;
    }

    auto& cfg = it->second;
    ProcessorPipeline* pipeline = nullptr;

    if (stage == "recall") pipeline = &cfg.recall_pipeline;
    else if (stage == "rank") pipeline = &cfg.rank_pipeline;
    else if (stage == "rerank") pipeline = &cfg.rerank_pipeline;
    else if (stage == "filter") pipeline = &cfg.filter_pipeline;
    else if (stage == "postprocess") pipeline = &cfg.postprocess_pipeline;
    else {
        LOG_ERROR("PipelineManager::HotReload: unknown stage '{}'", stage);
        return false;
    }

    std::string yaml_key = stage + "_stages";
    if (stage == "rank") yaml_key = "coarse_rank_stages";
    else if (stage == "rerank") yaml_key = "fine_rank_stages";

    return pipeline->LoadFromConfig(new_config, yaml_key);
}

} // namespace framework
} // namespace minisearchrec
