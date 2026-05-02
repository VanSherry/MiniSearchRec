// ============================================================
// MiniSearchRec - Session 基类实现
// ============================================================

#include "framework/session/session.h"
#include "utils/logger.h"
#include <chrono>
#include <random>
#include <sstream>

namespace minisearchrec {
namespace framework {

int32_t Session::Init(const CommonRequest& req) {
    begin_time = std::chrono::steady_clock::now();
    begin_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    request = req;
    uid = req.uid;
    query = req.query;
    business_type = req.business_type;
    scene = req.scene;
    req_num = req.req_num;
    search_id = req.search_id;

    // 生成 search_id（如果请求未携带）
    if (search_id.empty()) {
        search_id = std::to_string(begin_time_us);
    }

    // 生成 trace_id（框架层统一生成，所有业务通用）
    if (trace_id.empty()) {
        trace_id = GenerateTraceId();
    }

    auto init_end = std::chrono::steady_clock::now();
    metrics.init_cost_us = std::chrono::duration_cast<std::chrono::microseconds>(
        init_end - begin_time).count();

    // 设置超时 deadline（基于 system_clock 绝对时间）
    if (timeout_ms > 0) {
        deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + timeout_ms;
    }

    return 0;
}

void Session::BeforeDestruct() {
    auto now = std::chrono::steady_clock::now();
    metrics.total_cost_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now - begin_time).count();

    LOG_DEBUG("Session::BeforeDestruct: trace_id={}, business_type={}, query='{}', total_cost_us={}",
              trace_id, business_type, query, metrics.total_cost_us);
}

std::string Session::GenerateTraceId() {
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    thread_local static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1000, 9999);

    std::ostringstream oss;
    oss << "MSR-" << ts << "-" << dist(rng);
    return oss.str();
}

} // namespace framework
} // namespace minisearchrec
