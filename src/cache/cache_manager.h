// ============================================================
// MiniSearchRec - 统一缓存管理（双层架构）
// 对标：业界多级缓存方案（本地 LRU + 远端 Redis）
//
// 策略：
//   Get: L1(LRU) → L2(Redis) → miss
//   Set: L1(LRU) + L2(Redis) 同时写入
//   Redis 未配置时自动降级为纯 L1 模式
// ============================================================

#pragma once

#include <string>
#include <optional>
#include <memory>
#include "cache/lru_cache.h"
#include "cache/redis_client.h"
#include "search.pb.h"

namespace minisearchrec {

// ============================================================
// 缓存配置
// ============================================================
struct CacheConfig {
    // L1：本地 LRU
    size_t local_capacity = 1000;
    int local_ttl_seconds = 300;   // 本地缓存 TTL（秒）

    // L2：Redis（可选）
    bool redis_enabled = false;
    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;
    std::string redis_password;
    int redis_ttl_seconds = 600;   // Redis TTL
    int redis_pool_size = 4;
};

// ============================================================
// CacheManager：双层缓存
// ============================================================
class CacheManager {
public:
    // 默认构造：纯 L1 模式
    explicit CacheManager(size_t local_capacity = 1000);

    // 从配置初始化（含可选 Redis）
    bool Initialize(const CacheConfig& config);

    // ── 搜索结果缓存 ──
    std::optional<SearchResponse> GetSearchCache(const std::string& cache_key);
    void SetSearchCache(const std::string& cache_key,
                        const SearchResponse& response,
                        int ttl_seconds = -1);  // -1 使用默认 TTL

    // ── 通用 KV 缓存 ──
    std::optional<std::string> Get(const std::string& key);
    void Set(const std::string& key, const std::string& value, int ttl_seconds = -1);
    bool Delete(const std::string& key);

    // 生成 cache key
    std::string MakeCacheKey(const SearchRequest& req);

    // 清空所有缓存（L1 + L2）
    void Clear();

    // 获取统计信息
    struct Stats {
        uint64_t l1_hits = 0;
        uint64_t l2_hits = 0;
        uint64_t misses = 0;
        uint64_t l1_size = 0;
    };
    Stats GetStats() const;

    // Redis 是否可用
    bool IsRedisAvailable() const { return redis_client_ && redis_connected_; }

private:
    // L1：本地 LRU（带 TTL）
    LRUCache<std::string, std::string> local_cache_;

    // L2：Redis（可选）
    std::unique_ptr<RedisClient> redis_client_;
    bool redis_connected_ = false;

    // 配置
    CacheConfig config_;

    // 统计
    mutable std::mutex stats_mutex_;
    mutable Stats stats_;
};

} // namespace minisearchrec
