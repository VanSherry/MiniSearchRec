// ============================================================
// MiniSearchRec - Redis 客户端
// 参考：微信 Redis 客户端、X(Twitter) RedisWrapper
// 作用：提供 Redis 操作接口，支持缓存、计数器等
// ============================================================

#ifndef MINISEARCHREC_REDIS_CLIENT_H
#define MINISEARCHREC_REDIS_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

// 如果安装了 hiredis，定义 USE_HIREDIS
// 否则使用 HTTP 模拟（用于教学）
#ifndef USE_HIREDIS
// #define USE_HIREDIS  // 取消注释以启用真实 Redis 客户端
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
// ============================================================
class RedisClientFactory {
public:
    // 创建 Redis 客户端实例
    static std::unique_ptr<RedisClient> Create(bool use_mock = false);

    // 创建连接池
    static std::vector<std::unique_ptr<RedisClient>> CreatePool(
        const std::string& host, int port, const std::string& password,
        int pool_size);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_REDIS_CLIENT_H
