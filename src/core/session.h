// ============================================================
// MiniSearchRec - Session 上下文对象
// 参考：mmsearchqxcommon Session 设计
// 作用：贯穿整个请求生命周期的上下文，避免函数参数过长
// ============================================================

#ifndef MINISEARCHREC_SESSION_H
#define MINISEARCHREC_SESSION_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

// Proto 生成的头文件
#include "search.pb.h"
#include "user.pb.h"

namespace minisearchrec {

// ============================================================
// Query 解析结果
// 对应微信搜推的 QPInfo 结构
// ============================================================
struct QPInfo {
    std::string raw_query;                 // 原始查询词
    std::vector<std::string> terms;        // 分词结果
    std::string normalized_query;          // 归一化 query
    std::vector<float> query_embedding;    // query 向量（768维，可选）
    std::string inferred_category;         // 推断类别
    std::map<std::string, float> term_idf; // 每个词的 IDF 值（用于打分）
};

// ============================================================
// 单个候选文档
// 贯穿召回 → 粗排 → 精排 → 过滤全流程
// ============================================================
struct DocCandidate {
    std::string doc_id;
    float recall_score = 0.0f;         // 召回分
    float coarse_score = 0.0f;         // 粗排分
    float fine_score = 0.0f;           // 精排分
    float final_score = 0.0f;          // 最终分
    std::string recall_source;          // 召回来源（用于调试）

    // 特征向量（精排使用）
    std::vector<float> features;

    // 文档快照（避免重复查库）
    std::string title;
    std::string content_snippet;
    std::string author;
    int64_t publish_time = 0;
    std::string category;
    float quality_score = 0.0f;
    int64_t click_count = 0;
    int64_t like_count = 0;
    std::vector<float> embedding;  // 文档向量

    // 调试信息
    std::map<std::string, float> debug_scores;
};

// ============================================================
// 请求级 Session 上下文
// 对应微信搜推的 Session 对象，一次请求一个实例
// ============================================================
class Session {
public:
    Session();
    ~Session();

    // --- 输入阶段 ---
    std::string request_body;      // 原始请求体
    std::string trace_id;          // 链路追踪 ID
    int64_t start_time_ms;         // 请求开始时间

    // --- Proto 请求/响应 ---
    SearchRequest  request;
    SearchResponse response;

    // --- Query 理解阶段 ---
    QPInfo qp_info;

    // --- 用户画像 ---
    std::unique_ptr<UserProfile> user_profile;

    // --- 各阶段结果 ---
    std::vector<DocCandidate> recall_results;       // 召回结果
    std::vector<DocCandidate> coarse_rank_results;  // 粗排结果
    std::vector<DocCandidate> fine_rank_results;    // 精排结果
    std::vector<DocCandidate> final_results;        // 最终结果

    // --- 统计信息 ---
    struct StageStats {
        int64_t recall_cost_ms = 0;
        int64_t coarse_rank_cost_ms = 0;
        int64_t fine_rank_cost_ms = 0;
        int64_t filter_cost_ms = 0;
        int64_t total_cost_ms = 0;
    } stats;

    struct CountStats {
        int recall_count = 0;
        int coarse_count = 0;
        int fine_count = 0;
        int final_count = 0;
        std::map<std::string, int> recall_source_counts;
    } counts;

    // --- 配置快照（请求级）---
    std::string business_type = "default";

    // 生成 trace_id
    static std::string GenerateTraceId();
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SESSION_H
