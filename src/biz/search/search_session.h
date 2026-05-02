// ============================================================
// MiniSearchRec - SearchSession（垂搜业务 Session 子类）
// 位置：biz/search/（业务目录，不污染框架层）
//
// 继承 framework::Session，添加垂搜 Pipeline 专有字段
// 对标通用搜索框架，业务 Session 子类
// ============================================================

#ifndef MINISEARCHREC_SEARCH_SESSION_H
#define MINISEARCHREC_SEARCH_SESSION_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>

#include "framework/session/session.h"
#include "framework/processor/processor_interface.h"

// Proto 生成的头文件
#include "search.pb.h"
#include "user.pb.h"

namespace minisearchrec {

// ============================================================
// Query 解析结果（对标 QPInfo / QueryResponse）
// ============================================================
struct QPInfo {
    std::string raw_query;
    std::vector<std::string> terms;
    std::string normalized_query;
    std::vector<float> query_embedding;
    std::string inferred_category;
    std::map<std::string, float> term_idf;
};

// ============================================================
// 单个候选文档（贯穿召回→排序→过滤全流程）
// ============================================================
struct DocCandidate {
    std::string doc_id;
    float recall_score = 0.0f;
    float coarse_score = 0.0f;
    float fine_score = 0.0f;
    float final_score = 0.0f;
    std::string recall_source;

    std::vector<float> features;

    std::string title;
    std::string content_snippet;
    std::string author;
    int64_t publish_time = 0;
    std::string category;
    float quality_score = 0.0f;
    int64_t click_count = 0;
    int64_t like_count = 0;
    std::vector<float> embedding;

    std::map<std::string, float> debug_scores;
};

// ============================================================
// SearchSession：垂搜业务 Session 子类
// ============================================================
class SearchSession : public framework::Session {
public:
    SearchSession() = default;
    ~SearchSession() override = default;

    // --- Proto 请求/响应（垂搜专有）---
    SearchRequest search_request;
    SearchResponse search_response;

    // --- Query 理解结果 ---
    QPInfo qp_info;

    // --- 用户画像 ---
    std::unique_ptr<UserProfile> user_profile;

    // --- 候选文档列表（垂搜各阶段结果）---
    std::vector<DocCandidate> recall_results;
    std::vector<DocCandidate> coarse_rank_results;
    std::vector<DocCandidate> fine_rank_results;
    std::vector<DocCandidate> final_results;

    // --- 垂搜统计 ---
    struct SearchStats {
        int64_t recall_cost_ms = 0;
        int64_t coarse_rank_cost_ms = 0;
        int64_t fine_rank_cost_ms = 0;
        int64_t filter_cost_ms = 0;
        int64_t total_cost_ms = 0;
    } search_stats;

    struct SearchCounts {
        int recall_count = 0;
        int coarse_count = 0;
        int fine_count = 0;
        int final_count = 0;
        std::map<std::string, int> recall_source_counts;
    } search_counts;

    // --- A/B 实验参数覆盖 ---
    struct ABOverride {
        float mmr_lambda = -1.f;
        int coarse_top_k = -1;
        int fine_top_k = -1;
    } ab_override;
};

// ============================================================
// 兼容别名：旧代码中 Session = SearchSession
// ============================================================
using Session = SearchSession;

// ============================================================
// Processor 兼容基类（旧代码继承这些，实际转接到 framework::ProcessorInterface）
// ============================================================

// 召回处理器基类
class BaseRecallProcessor : public framework::ProcessorInterface {
public:
    ~BaseRecallProcessor() override = default;
    virtual int Process(Session& session) = 0;
    int Process(framework::Session* s) override {
        auto* ss = dynamic_cast<Session*>(s);
        if (!ss) return -1;
        return Process(*ss);
    }
};

// 打分处理器基类（粗排/精排）
class BaseScorerProcessor : public framework::ProcessorInterface {
public:
    ~BaseScorerProcessor() override = default;
    virtual int Process(Session& session, std::vector<DocCandidate>& candidates) = 0;
    int Process(framework::Session* s) override {
        auto* ss = dynamic_cast<Session*>(s);
        if (!ss) return -1;
        if (!ss->coarse_rank_results.empty())
            return Process(*ss, ss->coarse_rank_results);
        return Process(*ss, ss->recall_results);
    }
    virtual float Weight() const { return weight_; }
protected:
    float weight_ = 1.0f;
};

// 过滤处理器基类
class BaseFilterProcessor : public framework::ProcessorInterface {
public:
    ~BaseFilterProcessor() override = default;
    virtual bool ShouldKeep(const Session& session, const DocCandidate& candidate) = 0;
    int Process(framework::Session* s) override {
        auto* ss = dynamic_cast<Session*>(s);
        if (!ss) return -1;
        auto& results = ss->fine_rank_results.empty()
                        ? ss->coarse_rank_results : ss->fine_rank_results;
        auto it = std::remove_if(results.begin(), results.end(),
            [&](const DocCandidate& c) { return !ShouldKeep(*ss, c); });
        results.erase(it, results.end());
        return 0;
    }
};

// 后处理处理器基类
class BasePostProcessProcessor : public framework::ProcessorInterface {
public:
    ~BasePostProcessProcessor() override = default;
    virtual int Process(Session& session, std::vector<DocCandidate>& candidates) = 0;
    int Process(framework::Session* s) override {
        auto* ss = dynamic_cast<Session*>(s);
        if (!ss) return -1;
        return Process(*ss, ss->fine_rank_results);
    }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SEARCH_SESSION_H
