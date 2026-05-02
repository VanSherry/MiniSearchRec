// ============================================================
// MiniSearchRec - 搜索词热度存储
// 用于 Sug 词库构建 + Nav 热词来源
// 参考：业界搜索统计数据
// ============================================================

#ifndef MINISEARCHREC_QUERY_STATS_STORE_H
#define MINISEARCHREC_QUERY_STATS_STORE_H

#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace minisearchrec {

struct QueryStatItem {
    std::string query;
    int         freq      = 0;
    int64_t     last_time = 0;
    std::string source;   // title / tag / user_query
};

class QueryStatsStore {
public:
    static QueryStatsStore& Instance() {
        static QueryStatsStore inst;
        return inst;
    }

    bool Initialize(const std::string& db_path);
    void Close();

    // 增加词频（存在则+1，不存在则插入）
    bool IncrementQuery(const std::string& query, const std::string& source);

    // 批量初始化词库（from title / tag）
    bool BatchInsert(const std::vector<QueryStatItem>& items);

    // 获取前缀匹配的词（Sug 用）
    std::vector<QueryStatItem> GetByPrefix(const std::string& prefix, int limit = 30);

    // 获取热词 TOP-N（Nav 用）
    std::vector<QueryStatItem> GetTopN(int n);

    // 获取某分类相关的热词（Nav category_hot 用）
    std::vector<QueryStatItem> GetTopNBySource(const std::string& source, int n);

    // 获取总词条数
    int GetCount();

private:
    QueryStatsStore() = default;
    ~QueryStatsStore() { Close(); }

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    bool initialized_ = false;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_QUERY_STATS_STORE_H
