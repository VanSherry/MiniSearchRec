// =========================================================
// MiniSearchRec - Pipeline 编排器实现
// 参考：微信搜推 DAG 执行引擎
// =========================================================

#include "core/pipeline.h"
#include "core/config_manager.h"
#include "utils/logger.h"
#include <chrono>
#include <algorithm>
#include <numeric>

namespace minisearchrec {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // anonymous namespace

bool Pipeline::LoadConfig(const std::string& config_path) {
    try {
        raw_config_ = YAML::LoadFile(config_path);
    } catch (const YAML::Exception& e) {
        LOG_ERROR("Failed to load pipeline config: {} - {}", config_path, e.what());
        return false;
    }
    if (raw_config_.IsNull()) {
        LOG_ERROR("Pipeline config is empty: {}", config_path);
        return false;
    }
    return LoadFromNodes(raw_config_, YAML::Node(), YAML::Node());
}

bool Pipeline::LoadFromNodes(const YAML::Node& recall_cfg,
                              const YAML::Node& rank_cfg,
                              const YAML::Node& filter_cfg) {
    config_.recall_stages.clear();
    config_.coarse_rank_stages.clear();
    config_.fine_rank_stages.clear();
    config_.filter_stages.clear();
    config_.postprocess_stages.clear();

    // 加载召回阶段（来自 recall_cfg 或 raw_config_）
    auto parse_recall = [&](const YAML::Node& node) {
        if (!node || !node["recall_stages"]) return;
        for (const auto& n : node["recall_stages"]) {
            PipelineConfig::RecallStage stage;
            stage.name       = n["name"].as<std::string>();
            stage.enable     = n["enable"].as<bool>(true);
            stage.max_recall = n["max_recall"].as<int>(1000);
            stage.params     = n["params"];
            config_.recall_stages.push_back(stage);
        }
    };

    auto parse_rank = [&](const YAML::Node& node) {
        if (!node) return;
        if (node["coarse_rank_stages"]) {
            for (const auto& n : node["coarse_rank_stages"]) {
                PipelineConfig::CoarseRankStage stage;
                stage.name   = n["name"].as<std::string>();
                stage.weight = n["weight"].as<float>(1.0f);
                stage.params = n["params"];
                config_.coarse_rank_stages.push_back(stage);
            }
        }
        if (node["fine_rank_stages"]) {
            for (const auto& n : node["fine_rank_stages"]) {
                PipelineConfig::FineRankStage stage;
                stage.name       = n["name"].as<std::string>();
                stage.weight     = n["weight"].as<float>(1.0f);
                stage.model_path = n["model_path"].as<std::string>("");
                stage.params     = n["params"];
                config_.fine_rank_stages.push_back(stage);
            }
        }
        if (node["postprocess_stages"]) {
            for (const auto& n : node["postprocess_stages"]) {
                PipelineConfig::PostProcessStage stage;
                stage.name   = n["name"].as<std::string>();
                stage.params = n["params"];
                config_.postprocess_stages.push_back(stage);
            }
        }
    };

    auto parse_filter = [&](const YAML::Node& node) {
        if (!node || !node["filter_stages"]) return;
        for (const auto& n : node["filter_stages"]) {
            PipelineConfig::FilterStage stage;
            stage.name   = n["name"].as<std::string>();
            stage.params = n["params"];
            config_.filter_stages.push_back(stage);
        }
    };

    parse_recall(recall_cfg);
    parse_rank(rank_cfg);
    parse_filter(filter_cfg);

    // 全局配置优先从 rank_cfg 读取
    const YAML::Node* global = nullptr;
    if (rank_cfg && rank_cfg["final_result_count"]) global = &rank_cfg;
    else if (recall_cfg && recall_cfg["final_result_count"]) global = &recall_cfg;
    if (global) {
        config_.final_result_count = (*global)["final_result_count"].as<int>(20);
        config_.enable_cache       = (*global)["enable_cache"].as<bool>(true);
    }

    loaded_ = true;
    LOG_INFO("Pipeline loaded: recall={}, coarse={}, fine={}, filter={}, postproc={}",
             config_.recall_stages.size(),
             config_.coarse_rank_stages.size(),
             config_.fine_rank_stages.size(),
             config_.filter_stages.size(),
             config_.postprocess_stages.size());
    return true;
}

int Pipeline::Execute(Session& session) {
    int64_t start = NowMs();

    int ret = ExecuteRecall(session);
    if (ret != 0) {
        LOG_ERROR("ExecuteRecall failed with ret={}", ret);
        return ret;
    }

    ret = ExecuteCoarseRank(session);
    if (ret != 0) {
        LOG_ERROR("ExecuteCoarseRank failed with ret={}", ret);
        return ret;
    }

    ret = ExecuteFineRank(session);
    if (ret != 0) {
        LOG_ERROR("ExecuteFineRank failed with ret={}", ret);
        return ret;
    }

    ret = ExecuteFilter(session);
    if (ret != 0) {
        LOG_ERROR("ExecuteFilter failed with ret={}", ret);
        return ret;
    }

    ret = ExecutePostProcess(session);
    if (ret != 0) {
        LOG_ERROR("ExecutePostProcess failed with ret={}", ret);
        return ret;
    }

    BuildResponse(session);

    session.stats.total_cost_ms = NowMs() - start;

    LOG_INFO(
        "Pipeline done - trace_id={}, total={}ms, "
        "recall={}ms({}), coarse={}ms({}), "
        "fine={}ms({}), filter={}ms, final={}",
        session.trace_id,
        session.stats.total_cost_ms,
        session.stats.recall_cost_ms, session.counts.recall_count,
        session.stats.coarse_rank_cost_ms, session.counts.coarse_count,
        session.stats.fine_rank_cost_ms, session.counts.fine_count,
        session.stats.filter_cost_ms,
        session.final_results.size()
    );

    return 0;
}

int Pipeline::ExecuteRecall(Session& session) {
    int64_t start = NowMs();

    LOG_INFO("ExecuteRecall start - trace_id={}, query={}, stages={}",
             session.trace_id, session.request.query(), config_.recall_stages.size());

    for (const auto& stage : config_.recall_stages) {
        if (!stage.enable) {
            LOG_DEBUG("Recall stage {} is disabled, skipping", stage.name);
            continue;
        }

        int64_t processor_start = NowMs();

        auto processor = ProcessorFactory::Instance().CreateRecall(stage.name);
        if (!processor) {
            LOG_WARN("Failed to create recall processor: {}", stage.name);
            continue;
        }
        // 初始化处理器参数
        if (!processor->Init(stage.params)) {
            LOG_WARN("Recall processor {} Init() failed, skipping", stage.name);
            continue;
        }

        session.counts.recall_source_counts[stage.name] = 0;

        int ret = processor->Process(session);
        int64_t processor_cost = NowMs() - processor_start;

        if (ret != 0) {
            LOG_WARN("Recall processor {} failed with ret={}, cost={}ms",
                     stage.name, ret, processor_cost);
            continue;
        }

        LOG_INFO("Recall processor {} completed - cost={}ms, results={}",
                 stage.name, processor_cost, session.recall_results.size());
    }

    session.stats.recall_cost_ms = NowMs() - start;
    session.counts.recall_count = session.recall_results.size();

    LOG_INFO("ExecuteRecall completed - trace_id={}, cost={}ms, recall_count={}",
             session.trace_id, session.stats.recall_cost_ms, session.counts.recall_count);
    return 0;
}

int Pipeline::ExecuteCoarseRank(Session& session) {
    int64_t start = NowMs();

    if (session.recall_results.empty()) {
        LOG_INFO("ExecuteCoarseRank skipped - no recall results");
        session.stats.coarse_rank_cost_ms = 0;
        session.counts.coarse_count = 0;
        return 0;
    }

    LOG_INFO("ExecuteCoarseRank start - trace_id={}, input_count={}",
             session.trace_id, session.recall_results.size());

    for (const auto& stage : config_.coarse_rank_stages) {
        int64_t processor_start = NowMs();

        auto scorer = ProcessorFactory::Instance().CreateScorer(stage.name);
        if (!scorer) {
            LOG_WARN("Failed to create coarse rank scorer: {}", stage.name);
            continue;
        }
        if (!scorer->Init(stage.params)) {
            LOG_WARN("Coarse rank scorer {} Init() failed, skipping", stage.name);
            continue;
        }

        int ret = scorer->Process(session, session.recall_results);
        int64_t processor_cost = NowMs() - processor_start;

        if (ret != 0) {
            LOG_WARN("Coarse rank scorer {} failed with ret={}, cost={}ms",
                     stage.name, ret, processor_cost);
            continue;
        }

        LOG_INFO("Coarse rank scorer {} completed - cost={}ms",
                 stage.name, processor_cost);
    }

    std::sort(session.recall_results.begin(), session.recall_results.end(),
        [](const DocCandidate& a, const DocCandidate& b) {
            return a.coarse_score > b.coarse_score;
        });

    int keep = std::min(500, (int)session.recall_results.size());
    session.recall_results.resize(keep);
    session.coarse_rank_results = session.recall_results;

    session.stats.coarse_rank_cost_ms = NowMs() - start;
    session.counts.coarse_count = session.coarse_rank_results.size();

    LOG_INFO("ExecuteCoarseRank completed - trace_id={}, cost={}ms, coarse_count={}",
             session.trace_id, session.stats.coarse_rank_cost_ms, session.counts.coarse_count);
    return 0;
}

int Pipeline::ExecuteFineRank(Session& session) {
    int64_t start = NowMs();

    if (session.coarse_rank_results.empty()) {
        LOG_INFO("ExecuteFineRank skipped - no coarse rank results");
        session.stats.fine_rank_cost_ms = 0;
        session.counts.fine_count = 0;
        return 0;
    }

    LOG_INFO("ExecuteFineRank start - trace_id={}, input_count={}",
             session.trace_id, session.coarse_rank_results.size());

    for (const auto& stage : config_.fine_rank_stages) {
        int64_t processor_start = NowMs();

        auto scorer = ProcessorFactory::Instance().CreateScorer(stage.name);
        if (!scorer) {
            LOG_WARN("Failed to create fine rank scorer: {}", stage.name);
            continue;
        }
        if (!scorer->Init(stage.params)) {
            LOG_WARN("Fine rank scorer {} Init() failed, skipping", stage.name);
            continue;
        }

        int ret = scorer->Process(session, session.coarse_rank_results);
        int64_t processor_cost = NowMs() - processor_start;

        if (ret != 0) {
            LOG_WARN("Fine rank scorer {} failed with ret={}, cost={}ms",
                     stage.name, ret, processor_cost);
            continue;
        }

        LOG_INFO("Fine rank scorer {} completed - cost={}ms",
                 stage.name, processor_cost);
    }

    // 若无精排模型，用 coarse_score 作为 fine_score
    for (auto& cand : session.coarse_rank_results) {
        if (cand.fine_score == 0.0f) {
            cand.fine_score = cand.coarse_score;
        }
        cand.final_score = cand.fine_score;
    }

    std::sort(session.coarse_rank_results.begin(),
              session.coarse_rank_results.end(),
              [](const DocCandidate& a, const DocCandidate& b) {
                  return a.fine_score > b.fine_score;
              });

    int keep = std::min(100, (int)session.coarse_rank_results.size());
    session.coarse_rank_results.resize(keep);
    session.fine_rank_results = session.coarse_rank_results;

    session.stats.fine_rank_cost_ms = NowMs() - start;
    session.counts.fine_count = session.fine_rank_results.size();

    LOG_INFO("ExecuteFineRank completed - trace_id={}, cost={}ms, fine_count={}",
             session.trace_id, session.stats.fine_rank_cost_ms, session.counts.fine_count);
    return 0;
}

int Pipeline::ExecuteFilter(Session& session) {
    int64_t start = NowMs();

    if (session.fine_rank_results.empty()) {
        LOG_INFO("ExecuteFilter skipped - no fine rank results");
        session.stats.filter_cost_ms = 0;
        return 0;
    }

    LOG_INFO("ExecuteFilter start - trace_id={}, input_count={}",
             session.trace_id, session.fine_rank_results.size());

    size_t before_count = session.fine_rank_results.size();

    for (const auto& stage : config_.filter_stages) {
        int64_t processor_start = NowMs();

        auto filter = ProcessorFactory::Instance().CreateFilter(stage.name);
        if (!filter) {
            LOG_WARN("Failed to create filter: {}", stage.name);
            continue;
        }
        if (!filter->Init(stage.params)) {
            LOG_WARN("Filter {} Init() failed, skipping", stage.name);
            continue;
        }

        auto it = std::remove_if(
            session.fine_rank_results.begin(),
            session.fine_rank_results.end(),
            [&](const DocCandidate& cand) {
                return !filter->ShouldKeep(session, cand);
            });
        session.fine_rank_results.erase(it, session.fine_rank_results.end());

        int64_t processor_cost = NowMs() - processor_start;
        LOG_INFO("Filter {} completed - cost={}ms, removed={}",
                 stage.name, processor_cost,
                 before_count - session.fine_rank_results.size());
        before_count = session.fine_rank_results.size();
    }

    session.stats.filter_cost_ms = NowMs() - start;

    LOG_INFO("ExecuteFilter completed - trace_id={}, cost={}ms, remaining_count={}",
             session.trace_id, session.stats.filter_cost_ms,
             session.fine_rank_results.size());
    return 0;
}

int Pipeline::ExecutePostProcess(Session& session) {
    LOG_INFO("ExecutePostProcess start - trace_id={}, input_count={}",
             session.trace_id, session.fine_rank_results.size());

    for (const auto& stage : config_.postprocess_stages) {
        int64_t processor_start = NowMs();

        auto processor = ProcessorFactory::Instance().CreatePostProcess(stage.name);
        if (!processor) {
            LOG_WARN("Failed to create postprocess processor: {}", stage.name);
            continue;
        }
        if (!processor->Init(stage.params)) {
            LOG_WARN("PostProcess processor {} Init() failed, skipping", stage.name);
            continue;
        }

        int ret = processor->Process(session, session.fine_rank_results);
        int64_t processor_cost = NowMs() - processor_start;

        if (ret != 0) {
            LOG_WARN("Postprocess processor {} failed with ret={}, cost={}ms",
                     stage.name, ret, processor_cost);
            continue;
        }

        LOG_INFO("Postprocess processor {} completed - cost={}ms",
                 stage.name, processor_cost);
    }

    session.final_results = session.fine_rank_results;
    session.counts.final_count = session.final_results.size();

    LOG_INFO("ExecutePostProcess completed - trace_id={}, final_count={}",
             session.trace_id, session.counts.final_count);
    return 0;
}

void Pipeline::BuildResponse(Session& session) {
    session.response.set_ret(0);
    session.response.set_err_msg("");
    session.response.set_trace_id(session.trace_id);
    session.response.set_cost_ms(session.stats.total_cost_ms);

    int page = session.request.page() ? session.request.page() : 1;
    int page_size = session.request.page_size() ? session.request.page_size() : 20;

    int start_idx = (page - 1) * page_size;
    int end_idx = std::min(start_idx + page_size,
                           (int)session.final_results.size());

    for (int i = start_idx; i < end_idx && i < (int)session.final_results.size(); ++i) {
        const auto& cand = session.final_results[i];
        auto* result = session.response.add_results();

        result->set_doc_id(cand.doc_id);
        result->set_title(cand.title);
        result->set_snippet(cand.content_snippet);
        result->set_score(cand.final_score);
        result->set_recall_source(cand.recall_source);
        result->set_author(cand.author);
        result->set_publish_time(cand.publish_time);
        result->set_category(cand.category);
        result->set_click_count(cand.click_count);
        result->set_like_count(cand.like_count);

        for (const auto& [key, val] : cand.debug_scores) {
            (*result->mutable_debug_scores())[key] = val;
        }
    }

    session.response.set_total(session.final_results.size());
    session.response.set_page(page);
    session.response.set_page_size(page_size);
}

} // namespace minisearchrec
