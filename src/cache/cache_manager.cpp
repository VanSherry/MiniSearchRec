// ============================================================
// MiniSearchRec - CacheManager 双层缓存实现
// L1: 本地 LRU（带 TTL）
// L2: Redis（可配置，未配置时自动降级为纯 L1）
// ============================================================

#include "cache/cache_manager.h"
#include "utils/logger.h"
#include <functional>
#include <sstream>

namespace minisearchrec {

CacheManager::CacheManager(size_t local_capacity)
    : local_cache_(local_capacity) {
    config_.local_capacity = local_capacity;
}

bool CacheManager::Initialize(const CacheConfig& config) {
    config_ = config;

    // 重建本地缓存（容量可能变了）
    local_cache_.Clear();

    // 初始化 Redis（如果配置了）
    if (config_.redis_enabled) {
        redis_client_ = RedisClientFactory::Create(false);
        if (redis_client_) {
            redis_connected_ = redis_client_->Connect(
                config_.redis_host, config_.redis_port, config_.redis_password);
            if (redis_connected_) {
                LOG_INFO("CacheManager: Redis connected ({}:{})",
                         config_.redis_host, config_.redis_port);
            } else {
                LOG_WARN("CacheManager: Redis connect failed ({}:{}), fallback to L1 only",
                         config_.redis_host, config_.redis_port);
            }
        }
    } else {
        LOG_INFO("CacheManager: Redis not configured, using L1 only (capacity={})",
                 config_.local_capacity);
    }

    return true;
}

// ============================================================
// 通用 KV 缓存
// ============================================================
std::optional<std::string> CacheManager::Get(const std::string& key) {
    // L1 查询
    auto l1_result = local_cache_.Get(key);
    if (l1_result) {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        stats_.l1_hits++;
        return l1_result;
    }

    // L2 查询（Redis）
    if (redis_connected_ && redis_client_) {
        std::string value;
        if (redis_client_->Get(key, value)) {
            // 回填 L1
            local_cache_.Put(key, value, config_.local_ttl_seconds);
            std::lock_guard<std::mutex> lk(stats_mutex_);
            stats_.l2_hits++;
            return value;
        }
    }

    std::lock_guard<std::mutex> lk(stats_mutex_);
    stats_.misses++;
    return std::nullopt;
}

void CacheManager::Set(const std::string& key, const std::string& value, int ttl_seconds) {
    int l1_ttl = (ttl_seconds > 0) ? ttl_seconds : config_.local_ttl_seconds;
    int l2_ttl = (ttl_seconds > 0) ? ttl_seconds : config_.redis_ttl_seconds;

    // L1 写入
    local_cache_.Put(key, value, l1_ttl);

    // L2 写入（Redis）
    if (redis_connected_ && redis_client_) {
        redis_client_->Set(key, value, l2_ttl);
    }
}

bool CacheManager::Delete(const std::string& key) {
    bool deleted = local_cache_.Delete(key);
    if (redis_connected_ && redis_client_) {
        redis_client_->Del(key);
    }
    return deleted;
}

// ============================================================
// 搜索结果缓存
// ============================================================
std::optional<SearchResponse> CacheManager::GetSearchCache(const std::string& cache_key) {
    auto cached = Get(cache_key);
    if (!cached) {
        return std::nullopt;
    }

    SearchResponse response;
    if (!response.ParseFromString(cached.value())) {
        LOG_ERROR("CacheManager: failed to deserialize SearchResponse for key={}", cache_key);
        return std::nullopt;
    }

    return response;
}

void CacheManager::SetSearchCache(const std::string& cache_key,
                                  const SearchResponse& response,
                                  int ttl_seconds) {
    std::string serialized;
    if (!response.SerializeToString(&serialized)) {
        LOG_ERROR("CacheManager: failed to serialize SearchResponse for key={}", cache_key);
        return;
    }

    Set(cache_key, serialized, ttl_seconds);
}

std::string CacheManager::MakeCacheKey(const SearchRequest& req) {
    // 使用 hash(uid:query:page:page_size:business_type) 作为 key
    std::ostringstream oss;
    oss << (req.uid().empty() ? "_" : req.uid()) << ":"
        << req.query() << ":"
        << req.page() << ":"
        << req.page_size() << ":"
        << req.business_type();
    return oss.str();
}

void CacheManager::Clear() {
    local_cache_.Clear();
    // Redis 不做全量清空（生产环境危险操作）
    LOG_INFO("CacheManager: L1 cache cleared");
}

CacheManager::Stats CacheManager::GetStats() const {
    std::lock_guard<std::mutex> lk(stats_mutex_);
    Stats s = stats_;
    s.l1_size = local_cache_.Size();
    return s;
}

} // namespace minisearchrec
