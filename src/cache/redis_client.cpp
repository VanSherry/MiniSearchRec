// ============================================================
// MiniSearchRec - Redis 客户端实现
// InMemoryRedisClient：本地内存后端（Redis 未配置时的降级方案）
// 支持 TTL 过期、线程安全、完整 Redis 接口
// ============================================================

#include "cache/redis_client.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <cstring>

namespace minisearchrec {

// ============================================================
// InMemoryRedisClient：本地内存后端
// 用于 Redis 未配置或不可用时的降级方案
// 完整实现所有 Redis 接口，支持 TTL 过期
// ============================================================
class InMemoryRedisClient : public RedisClient {
public:
    InMemoryRedisClient() = default;
    ~InMemoryRedisClient() override = default;

    bool Connect(const std::string& host, int port,
                  const std::string& password) override {
        host_ = host;
        port_ = port;
        connected_ = true;
        return true;
    }

    void Disconnect() override {
        connected_ = false;
    }

    bool Set(const std::string& key, const std::string& value,
             int ttl_seconds = -1) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        store_[key] = value;
        if (ttl_seconds > 0) {
            expirations_[key] = std::chrono::steady_clock::now() + 
                                std::chrono::seconds(ttl_seconds);
        }
        return true;
    }

    bool Get(const std::string& key, std::string& value) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查过期
        auto exp_it = expirations_.find(key);
        if (exp_it != expirations_.end()) {
            if (std::chrono::steady_clock::now() > exp_it->second) {
                store_.erase(key);
                expirations_.erase(exp_it);
                return false;
            }
        }
        
        auto it = store_.find(key);
        if (it != store_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    bool Del(const std::string& key) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        store_.erase(key);
        expirations_.erase(key);
        return true;
    }

    bool Exists(const std::string& key) override {
        std::string value;
        return Get(key, value);
    }

    bool Expire(const std::string& key, int ttl_seconds) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (store_.find(key) == store_.end()) return false;
        expirations_[key] = std::chrono::steady_clock::now() + 
                            std::chrono::seconds(ttl_seconds);
        return true;
    }

    int64_t Incr(const std::string& key, int64_t delta = 1) override {
        if (!connected_) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        std::string& val = store_[key];
        int64_t current = 0;
        if (!val.empty()) {
            try { current = std::stoll(val); } catch (...) { current = 0; }
        }
        current += delta;
        val = std::to_string(current);
        return current;
    }

    int64_t Decr(const std::string& key, int64_t delta = 1) override {
        return Incr(key, -delta);
    }

    bool HSet(const std::string& key, const std::string& field,
               const std::string& value) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        hash_store_[key][field] = value;
        return true;
    }

    bool HGet(const std::string& key, const std::string& field,
               std::string& value) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto key_it = hash_store_.find(key);
        if (key_it == hash_store_.end()) return false;
        auto field_it = key_it->second.find(field);
        if (field_it == key_it->second.end()) return false;
        value = field_it->second;
        return true;
    }

    bool HGetAll(const std::string& key,
                   std::vector<std::pair<std::string, std::string>>& fields) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        fields.clear();
        auto key_it = hash_store_.find(key);
        if (key_it == hash_store_.end()) return false;
        for (const auto& pair : key_it->second) {
            fields.emplace_back(pair.first, pair.second);
        }
        return true;
    }

    bool LPush(const std::string& key, const std::string& value) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        list_store_[key].insert(list_store_[key].begin(), value);
        // 限制列表长度
        if (list_store_[key].size() > 1000) {
            list_store_[key].resize(1000);
        }
        return true;
    }

    bool RPush(const std::string& key, const std::string& value) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        list_store_[key].push_back(value);
        if (list_store_[key].size() > 1000) {
            list_store_[key].erase(list_store_[key].begin());
        }
        return true;
    }

    bool LPop(const std::string& key, std::string& value) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = list_store_.find(key);
        if (it == list_store_.end() || it->second.empty()) return false;
        value = it->second.front();
        it->second.erase(it->second.begin());
        return true;
    }

    int64_t LLen(const std::string& key) override {
        if (!connected_) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = list_store_.find(key);
        if (it == list_store_.end()) return 0;
        return static_cast<int64_t>(it->second.size());
    }

    bool SAdd(const std::string& key, const std::string& member) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        set_store_[key].insert(member);
        return true;
    }

    bool SIsMember(const std::string& key, const std::string& member) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = set_store_.find(key);
        if (it == set_store_.end()) return false;
        return it->second.find(member) != it->second.end();
    }

    bool ZAdd(const std::string& key, double score,
                const std::string& member) override {
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        zset_store_[key][member] = score;
        return true;
    }

    int64_t ZRank(const std::string& key, const std::string& member) override {
        if (!connected_) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = zset_store_.find(key);
        if (it == zset_store_.end()) return -1;
        
        // 按 score 排序后查找 rank
        std::vector<std::pair<std::string, double>> sorted;
        for (const auto& pair : it->second) {
            sorted.emplace_back(pair.first, pair.second);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) {
                      return a.second > b.second;  // 降序
                  });
        
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i].first == member) {
                return static_cast<int64_t>(i);
            }
        }
        return -1;
    }

    bool MGet(const std::vector<std::string>& keys,
                std::vector<std::string>& values) override {
        values.clear();
        for (const auto& key : keys) {
            std::string val;
            if (Get(key, val)) {
                values.push_back(val);
            } else {
                values.push_back("");
            }
        }
        return true;
    }

    bool MSet(const std::vector<std::pair<std::string, std::string>>& kvs) override {
        for (const auto& kv : kvs) {
            if (!Set(kv.first, kv.second)) {
                return false;
            }
        }
        return true;
    }

    bool Ping() override {
        return connected_;
    }

    std::string GetLastError() const override {
        return last_error_;
    }

private:
    std::string host_;
    int port_ = 6379;
    bool connected_ = false;
    std::string last_error_;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> store_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> expirations_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_store_;
    std::unordered_map<std::string, std::vector<std::string>> list_store_;
    std::unordered_map<std::string, std::unordered_set<std::string>> set_store_;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> zset_store_;
};

// ============================================================
// 工厂实现
// ============================================================
std::unique_ptr<RedisClient> RedisClientFactory::Create(bool use_local_backend) {
#ifdef USE_HIREDIS
    if (!use_local_backend) {
        // HiredisClient 需链接 hiredis 库：cmake -DUSE_HIREDIS=ON
        // return std::make_unique<HiredisClient>();
    }
#endif
    // 默认/降级：使用本地内存后端
    return std::unique_ptr<RedisClient>(new InMemoryRedisClient());
}

std::vector<std::unique_ptr<RedisClient>> RedisClientFactory::CreatePool(
    const std::string& host, int port, const std::string& password,
    int pool_size) {
    std::vector<std::unique_ptr<RedisClient>> pool;
    for (int i = 0; i < pool_size; ++i) {
        auto client = Create(false);
        client->Connect(host, port, password);
        pool.push_back(std::move(client));
    }
    return pool;
}

} // namespace minisearchrec
