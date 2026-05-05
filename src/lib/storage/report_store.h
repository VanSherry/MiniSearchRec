// ============================================================
// MiniSearchRec - 异步曝光/点击上报存储
// 职责：各业务（search/sug/hint/nav）在 Report() 中推入事件，
//       后台线程异步批量写入 SQLite，不阻塞主流程
// ============================================================

#ifndef MINISEARCHREC_REPORT_STORE_H
#define MINISEARCHREC_REPORT_STORE_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

// 前向声明
struct sqlite3;

namespace minisearchrec {

struct ReportEvent {
    std::string biz_type;    // search / sug / hint / nav
    std::string event_type;  // impression / click
    int         count = 0;
    int64_t     ts = 0;
};

class ReportStore {
public:
    static ReportStore& Instance() {
        static ReportStore inst;
        return inst;
    }

    // 初始化：打开数据库，启动后台线程
    bool Initialize(const std::string& db_path);

    // 异步上报一条事件（非阻塞，仅入队）
    void Report(const std::string& biz_type,
                const std::string& event_type,
                int count);

    // 查询时间序列数据（按 biz_type + 时间范围）
    // 返回 JSON 字符串
    std::string QueryTimeSeries(const std::string& biz_type,
                                int hours,
                                int bucket_sec);

    // 停止后台线程并刷盘
    void Stop();

private:
    ReportStore() = default;
    ~ReportStore() { Stop(); }

    // 后台线程：批量消费队列并写入 SQLite
    void FlushLoop();

    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<ReportEvent> queue_;

    std::thread worker_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_REPORT_STORE_H
