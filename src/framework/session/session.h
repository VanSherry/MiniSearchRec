// ============================================================
// MiniSearchRec - Session 基类
// 对标：通用搜索框架 Session
//
// 设计原则（完全
//   1. 基类包含所有通用搜索链字段（recall/rank/filter/interpose）
//   2. 子类只添加业务特有字段
//   3. Handler 通过 dynamic_cast<具体Session*> 访问子类
//   4. Processor 链直接操作基类字段
//
// 使用方式：
//   - 轻业务（sug/hint/nav）：直接使用 framework::Session 或轻量子类
//   - 重业务（search）：继承添加 QPInfo/DocCandidate 等垂搜字段
// ============================================================

#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace minisearchrec {
namespace framework {

// ============================================================
// 请求公共字段（对标 QXSearchReq::CommonReq）
// ============================================================
struct CommonRequest {
    std::string query;
    std::string uid;
    uint64_t business_type = 0;
    uint32_t scene = 0;
    int32_t req_num = 10;
    int32_t page = 1;
    int32_t page_size = 10;
    std::string search_id;
    std::string raw_body;
    std::unordered_map<std::string, std::string> extra;
};

// ============================================================
// 响应公共字段（对标 QXSearchResp::CommonResp）
// ============================================================
struct CommonResponse {
    int32_t ret = 0;
    std::string err_msg;
    int32_t total = 0;
    std::string search_id;
    std::string items_json;
    std::unordered_map<std::string, std::string> extra;
};

// ============================================================
// 阶段耗时统计
// ============================================================
struct StageMetrics {
    int64_t init_cost_us = 0;
    int64_t presearch_cost_us = 0;
    int64_t search_cost_us = 0;
    int64_t rank_cost_us = 0;
    int64_t rerank_cost_us = 0;
    int64_t interpose_cost_us = 0;
    int64_t response_cost_us = 0;
    int64_t total_cost_us = 0;
};

// ============================================================
// 排序输入/输出（
// 通用的排序数据结构，所有业务共用
// ============================================================
struct RankItem {
    std::string word;           // 词条内容
    float score = 0.0f;        // 最终排序分
    int32_t rank = 0;          // 排序位次
    uint32_t source = 0;       // 召回来源
    std::string id;            // 唯一标识
    std::unordered_map<std::string, float> features;   // 特征
    std::unordered_map<std::string, std::string> ext;  // 扩展字段
};

struct RankInput {
    std::vector<RankItem> items;
};

struct RankOutput {
    std::vector<RankItem> items;
    std::vector<RankItem> filter_items;  // 被过滤掉的结果（用于诊断）
};

// ============================================================
// 干预 Session（对标 InterposeSession）
// ============================================================
struct InterposeSessionData {
    struct FilterItem {
        std::string word;
        std::string reason;
        std::string ext_info;
    };
    std::vector<FilterItem> filter_items;
    std::vector<RankItem> force_top_items;  // 仅出/强插词条
};

// ============================================================
// Session 基类
// ============================================================
struct Session {
    Session() = default;
    virtual ~Session() = default;

    // ── 生命周期回调 ──
    virtual int32_t Init(const CommonRequest& req);
    virtual void BeforeDestruct();

    // ── 业务可覆写 ──
    virtual bool NeedPersonalize() const { return !uid.empty(); }

    // ── 通用 KV 存储 ──
    void Set(const std::string& key, const std::string& value) { kv_store_[key] = value; }
    std::string Get(const std::string& key) const {
        auto it = kv_store_.find(key);
        return (it != kv_store_.end()) ? it->second : "";
    }

    // ── Any 存储（复杂对象传递）──
    void SetAny(const std::string& key, std::any value) { any_store_[key] = std::move(value); }
    template <typename T>
    T* GetAny(const std::string& key) {
        auto it = any_store_.find(key);
        if (it == any_store_.end()) return nullptr;
        try { return std::any_cast<T>(&it->second); } catch (...) { return nullptr; }
    }

    // ================================================================
    // 公共字段（
    // ================================================================

    // ── 基础请求信息 ──
    std::string uid;
    std::string query;
    uint64_t business_type = 0;
    uint32_t scene = 0;
    int32_t req_num = 10;
    std::string search_id;
    std::string trace_id;          // 链路追踪 ID（所有业务通用）

    // ── 生成 trace_id（框架层通用能力）──
    static std::string GenerateTraceId();

    // 请求/响应
    CommonRequest request;
    CommonResponse response;

    // ── Recall（召回）层字段 —— 对标 m_retrieve_resp ──
    RankInput retrieve_resp;  // 召回结果（所有召回路合并后）

    // ── Rank（排序）层字段 —— 对标 m_input / m_output ──
    RankInput rank_input;     // 排序输入
    RankOutput rank_output;   // 排序输出

    // ── Rerank（重排序）层字段 —— 对标 m_rerank_input / m_rerank_output ──
    RankInput rerank_input;
    RankOutput rerank_output;

    // ── Interpose（干预）层字段 —— 对标 m_interpose_session ──
    InterposeSessionData interpose_data;

    // ── 阶段耗时 ──
    StageMetrics metrics;

    // ── 时间戳 ──
    std::chrono::steady_clock::time_point begin_time;
    uint64_t begin_time_us = 0;

    // ── 超时控制 ──
    int64_t deadline_ms = 0;
    int64_t timeout_ms = 200;

    bool IsTimedOut() const {
        if (deadline_ms == 0) return false;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        return now >= deadline_ms;
    }

    // ── 结果数统计 ──
    struct CountStats {
        int retrieve_count = 0;
        int rank_count = 0;
        int rerank_count = 0;
        int final_count = 0;
        std::map<std::string, int> source_counts;
    } counts;

private:
    std::unordered_map<std::string, std::string> kv_store_;
    std::unordered_map<std::string, std::any> any_store_;
};

} // namespace framework
} // namespace minisearchrec
