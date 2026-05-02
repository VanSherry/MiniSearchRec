// ============================================================
// MiniSearchRec - LRU 缓存模板类（支持 TTL 过期）
// 对标：业界 LRU 本地缓存
// ============================================================

#pragma once

#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <chrono>

namespace minisearchrec {

template <typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    // 查询缓存（自动跳过 TTL 过期条目）
    std::optional<V> Get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;

        // 检查 TTL 过期
        if (it->second->expire_time != TimePoint{} &&
            std::chrono::steady_clock::now() > it->second->expire_time) {
            list_.erase(it->second);
            map_.erase(it);
            return std::nullopt;
        }

        // 移到链表头（最近使用）
        list_.splice(list_.begin(), list_, it->second);
        return it->second->value;
    }

    // 写入缓存（支持 TTL，ttl_seconds <= 0 表示永不过期）
    void Put(const K& key, const V& value, int ttl_seconds = 0) {
        std::lock_guard<std::mutex> lock(mutex_);

        TimePoint expire_time{};
        if (ttl_seconds > 0) {
            expire_time = std::chrono::steady_clock::now() +
                          std::chrono::seconds(ttl_seconds);
        }

        // 已存在 → 更新
        auto it = map_.find(key);
        if (it != map_.end()) {
            list_.splice(list_.begin(), list_, it->second);
            it->second->value = value;
            it->second->expire_time = expire_time;
            return;
        }

        // 淘汰：优先淘汰已过期的，否则淘汰最久未用
        while (list_.size() >= capacity_) {
            EvictOneLocked();
        }

        // 插入新节点
        list_.emplace_front(CacheEntry{key, value, expire_time});
        map_[key] = list_.begin();
    }

    bool Delete(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        list_.erase(it->second);
        map_.erase(it);
        return true;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        list_.clear();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

    // 主动清理过期条目（可由后台线程定时调用）
    size_t EvictExpired() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        size_t evicted = 0;
        auto it = list_.begin();
        while (it != list_.end()) {
            if (it->expire_time != TimePoint{} && now > it->expire_time) {
                map_.erase(it->key);
                it = list_.erase(it);
                ++evicted;
            } else {
                ++it;
            }
        }
        return evicted;
    }

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    struct CacheEntry {
        K key;
        V value;
        TimePoint expire_time;  // {} 表示永不过期
    };

    void EvictOneLocked() {
        if (list_.empty()) return;
        // 先找过期的
        auto now = std::chrono::steady_clock::now();
        for (auto it = list_.rbegin(); it != list_.rend(); ++it) {
            if (it->expire_time != TimePoint{} && now > it->expire_time) {
                map_.erase(it->key);
                list_.erase(std::next(it).base());
                return;
            }
        }
        // 没有过期的 → 淘汰最久未用
        map_.erase(list_.back().key);
        list_.pop_back();
    }

    size_t capacity_;
    std::list<CacheEntry> list_;
    std::unordered_map<K, typename std::list<CacheEntry>::iterator> map_;
    mutable std::mutex mutex_;
};

} // namespace minisearchrec
