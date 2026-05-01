// ============================================================
// MiniSearchRec - 哈希工具
// 提供各种哈希函数实现
// ============================================================

#ifndef MINISEARCHREC_HASH_UTILS_H
#define MINISEARCHREC_HASH_UTILS_H

#include <string>
#include <cstdint>
#include <functional>

namespace minisearchrec {
namespace utils {

// MurmurHash3 32位哈希
uint32_t MurmurHash3(const std::string& key, uint32_t seed = 0);

// MurmurHash3 128位哈希（返回两个64位整数）
void MurmurHash3_x64_128(const void* key, int len, uint32_t seed, uint64_t* out);

// DJB2 哈希
uint32_t DJB2Hash(const std::string& str);

// SDBM 哈希
uint32_t SDBMHash(const std::string& str);

// FNV-1a 32位哈希
uint32_t FNV1aHash32(const std::string& str);

// FNV-1a 64位哈希
uint64_t FNV1aHash64(const std::string& str);

// CRC32 哈希
uint32_t CRC32Hash(const std::string& str);

// 简单哈希（用于教学）
uint32_t SimpleHash(const std::string& str);

// 将哈希值映射到指定范围 [0, range-1]
uint32_t HashToRange(uint32_t hash, uint32_t range);

// 一致性哈希：计算虚拟节点哈希
uint32_t ConsistentHash(const std::string& key, int virtual_node_count, uint32_t range);

// 布隆过滤器哈希（使用多个哈希函数）
void BloomHash(const std::string& key, int k, uint64_t m, std::vector<uint64_t>& positions);

// 字符串哈希（用于 unordered_map）
struct StringHash {
    size_t operator()(const std::string& str) const;
};

// 计算 MD5 哈希（返回十六进制字符串）
std::string MD5Hash(const std::string& str);

// 计算 SHA-1 哈希（返回十六进制字符串）
std::string SHA1Hash(const std::string& str);

} // namespace utils
} // namespace minisearchrec

#endif // MINISEARCHREC_HASH_UTILS_H
