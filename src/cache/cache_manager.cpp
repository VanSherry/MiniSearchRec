// =========================================================
// MiniSearchRec - 缓存管理器实现
// =========================================================

#include "cache/cache_manager.h"
#include "utils/logger.h"
#include <functional>
#include <iomanip>
#include <sstream>
#include <google/protobuf/message.h>

namespace minisearchrec {

CacheManager::CacheManager(size_t local_capacity)
    : local_cache_(local_capacity) {}

std::optional<SearchResponse> CacheManager::GetSearchCache(
    const std::string& cache_key) {
    auto cached = local_cache_.Get(cache_key);
    if (!cached) {
        LOG_DEBUG("Cache miss for key: {}", cache_key);
        return std::nullopt;
    }

    SearchResponse response;
    if (!response.ParseFromString(cached.value())) {
        LOG_ERROR("Failed to deserialize SearchResponse for key: {}", cache_key);
        return std::nullopt;
    }

    LOG_DEBUG("Cache hit for key: {}", cache_key);
    return response;
}

void CacheManager::SetSearchCache(const std::string& cache_key,
                                  const SearchResponse& response,
                                  int ttl_seconds) {
    std::string serialized;
    if (!response.SerializeToString(&serialized)) {
        LOG_ERROR("Failed to serialize SearchResponse for key: {}", cache_key);
        return;
    }

    local_cache_.Put(cache_key, serialized);
    LOG_DEBUG("Cache set for key: {}, ttl: {} seconds", cache_key, ttl_seconds);
}

std::string CacheManager::MakeCacheKey(const SearchRequest& req) {
    std::ostringstream oss;
    oss << (req.uid().empty() ? "anon" : req.uid()) << ":"
        << req.query() << ":"
        << req.page() << ":"
        << req.page_size() << ":"
        << req.business_type();
    return oss.str();
}

void CacheManager::Clear() {
    LOG_INFO("CacheManager::Clear called");
    local_cache_.Clear();
}

} // namespace minisearchrec
