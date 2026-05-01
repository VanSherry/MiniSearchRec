// ============================================================
// MiniSearchRec - LRU 缓存模板类
// 参考：微信 FKV + MKV 双层缓存设计
// ============================================================

#ifndef MINISEARCHREC_LRU_CACHE_H
#define MINISEARCHREC_LRU_CACHE_H

#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace minisearchrec {

// ============================================================
// LRU 缓存（模板实现）
// Key: 缓存键类型，Value: 缓存值类型
// ============================================================
template <typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    // 查询缓存
    std::optional<V> Get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;

        // 移到链表头（最近使用）
        list_.splice(list_.begin(), list_, it->second);
        return it->second->second;
    }

    // 写入缓存
    void Put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 如果已存在，更新并移到链表头
        if (map_.count(key)) {
            list_.splice(list_.begin(), list_, map_[key]);
            map_[key]->second = value;
            return;
        }

        // 淘汰最久未用
        if (list_.size() >= capacity_) {
            auto& node = list_.back();
            map_.erase(node.first);
            list_.pop_back();
        }

        // 插入新节点到链表头
        list_.emplace_front(key, value);
        map_[key] = list_.begin();
    }

    // 删除缓存
    bool Delete(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        list_.erase(it->second);
        map_.erase(it);
        return true;
    }

    // 清空缓存
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        list_.clear();
    }

    // 获取当前大小
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

private:
    size_t capacity_;
    // list 存储键值对，最近使用的在头部
    std::list<std::pair<K, V>> list_;
    // map 从 key 到 list 迭代器
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
    mutable std::mutex mutex_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_LRU_CACHE_H
