// ============================================================
// MiniSearchRec - 统一缓存管理
// 参考：微信 FKV + MKV 双层缓存设计
// ============================================================

#ifndef MINISEARCHREC_CACHE_MANAGER_H
#define MINISEARCHREC_CACHE_MANAGER_H

#include <string>
#include <optional>
#include "cache/lru_cache.h"
#include "search.pb.h"

namespace minisearchrec {

// ============================================================
// 缓存管理器（本地 LRU + 可选 Redis）
// ============================================================
class CacheManager {
public:
    explicit CacheManager(size_t local_capacity = 100);
    ~CacheManager() = default;

    // 查询搜索缓存
    std::optional<SearchResponse> GetSearchCache(const std::string& cache_key);

    // 写入搜索缓存
    void SetSearchCache(const std::string& cache_key,
                        const SearchResponse& response,
                        int ttl_seconds = 300);

    // 生成 cache key：hash(uid + query + page + business_type)
    std::string MakeCacheKey(const SearchRequest& req);

    // 清空缓存
    void Clear();

private:
    LRUCache<std::string, std::string> local_cache_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_CACHE_MANAGER_H
