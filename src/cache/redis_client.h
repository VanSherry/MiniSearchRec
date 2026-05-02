// ============================================================
// MiniSearchRec - Redis 客户端
// 对标：业界 Redis 缓存客户端
// 策略：
//   - 编译时有 hiredis → 使用真实 Redis 连接
//   - 无 hiredis → 使用本地内存后端（InMemoryRedisClient）
//   - 通过配置动态决定是否启用 Redis
// ============================================================

#ifndef MINISEARCHREC_REDIS_CLIENT_H
#define MINISEARCHREC_REDIS_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

// 编译时定义 USE_HIREDIS 以启用真实 Redis 连接
// 未定义时使用 InMemoryRedisClient（本地内存后端，支持所有接口）
#ifndef USE_HIREDIS
// 自动检测：如果安装了 hiredis 则启用
// cmake 中通过 find_library(HIREDIS_LIB hiredis) 控制
#endif

namespace minisearchrec {

// ============================================================
// Redis 客户端（接口）
// ============================================================
class RedisClient {
public:
    RedisClient() = default;
    virtual ~RedisClient() = default;

    // 连接 Redis
    virtual bool Connect(const std::string& host, int port, 
                          const std::string& password = "") = 0;

    // 断开连接
    virtual void Disconnect() = 0;

    // === 字符串操作 ===
    // 设置键值
    virtual bool Set(const std::string& key, const std::string& value, 
                     int ttl_seconds = -1) = 0;

    // 获取值
    virtual bool Get(const std::string& key, std::string& value) = 0;

    // 删除键
    virtual bool Del(const std::string& key) = 0;

    // 检查键是否存在
    virtual bool Exists(const std::string& key) = 0;

    // 设置过期时间
    virtual bool Expire(const std::string& key, int ttl_seconds) = 0;

    // === 计数器操作 ===
    // 递增
    virtual int64_t Incr(const std::string& key, int64_t delta = 1) = 0;

    // 递减
    virtual int64_t Decr(const std::string& key, int64_t delta = 1) = 0;

    // === 哈希操作 ===
    // 设置哈希字段
    virtual bool HSet(const std::string& key, const std::string& field,
                       const std::string& value) = 0;

    // 获取哈希字段
    virtual bool HGet(const std::string& key, const std::string& field,
                       std::string& value) = 0;

    // 获取所有哈希字段
    virtual bool HGetAll(const std::string& key, 
                           std::vector<std::pair<std::string, std::string>>& fields) = 0;

    // === 列表操作 ===
    // 从左侧推入
    virtual bool LPush(const std::string& key, const std::string& value) = 0;

    // 从右侧推入
    virtual bool RPush(const std::string& key, const std::string& value) = 0;

    // 从左侧弹出
    virtual bool LPop(const std::string& key, std::string& value) = 0;

    // 获取列表长度
    virtual int64_t LLen(const std::string& key) = 0;

    // === 集合操作 ===
    // 添加集合成员
    virtual bool SAdd(const std::string& key, const std::string& member) = 0;

    // 检查是否是集合成员
    virtual bool SIsMember(const std::string& key, const std::string& member) = 0;

    // === 有序集合操作 ===
    // 添加有序集合成员
    virtual bool ZAdd(const std::string& key, double score, 
                        const std::string& member) = 0;

    // 获取有序集合排名
    virtual int64_t ZRank(const std::string& key, const std::string& member) = 0;

    // === 批量操作 ===
    // 批量获取
    virtual bool MGet(const std::vector<std::string>& keys,
                        std::vector<std::string>& values) = 0;

    // 批量设置
    virtual bool MSet(const std::vector<std::pair<std::string, std::string>>& kvs) = 0;

    // Ping
    virtual bool Ping() = 0;

    // 获取错误信息
    virtual std::string GetLastError() const = 0;
};

// ============================================================
// Redis 客户端工厂
// use_local_backend=true 时强制使用本地内存后端
// use_local_backend=false 且编译了 hiredis 时使用真实 Redis
// ============================================================
class RedisClientFactory {
public:
    static std::unique_ptr<RedisClient> Create(bool use_local_backend = false);

    static std::vector<std::unique_ptr<RedisClient>> CreatePool(
        const std::string& host, int port, const std::string& password,
        int pool_size);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_REDIS_CLIENT_H
