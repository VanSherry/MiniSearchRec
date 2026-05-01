// ============================================================
// MiniSearchRec - 哈希工具实现
// ============================================================

#include "utils/hash_utils.h"
#include <cstring>
#include <vector>

namespace minisearchrec {
namespace utils {

// MurmurHash3 实现 (32位)
uint32_t MurmurHash3(const std::string& key, uint32_t seed) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(key.data());
    int len = static_cast<int>(key.length());
    
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    const uint32_t* blocks = reinterpret_cast<const uint32_t*>(data + nblocks * 4);
    
    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);
        h1 = h1 * 5 + 0xe6546b64;
    }
    
    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;
    
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = (k1 << 15) | (k1 >> 17);
                k1 *= c2;
                h1 ^= k1;
    }
    
    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    
    return h1;
}

// MurmurHash3 128位实现 (x64)
void MurmurHash3_x64_128(const void* key, int len, uint32_t seed, uint64_t* out) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(key);
    const int nblocks = len / 16;
    
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;
    
    const uint64_t* blocks = reinterpret_cast<const uint64_t*>(data);
    
    for (int i = 0; i < nblocks; i++) {
        uint64_t k1 = blocks[i * 2];
        uint64_t k2 = blocks[i * 2 + 1];
        
        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> 33);
        k1 *= c2;
        h1 ^= k1;
        
        h1 = (h1 << 27) | (h1 >> 37);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;
        
        k2 *= c2;
        k2 = (k2 << 33) | (k2 >> 31);
        k2 *= c1;
        h2 ^= k2;
        
        h2 = (h2 << 31) | (h2 >> 33);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }
    
    const uint8_t* tail = data + nblocks * 16;
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    
    switch (len & 15) {
        case 15: k2 ^= static_cast<uint64_t>(tail[14]) << 48;
        case 14: k2 ^= static_cast<uint64_t>(tail[13]) << 40;
        case 13: k2 ^= static_cast<uint64_t>(tail[12]) << 32;
        case 12: k2 ^= static_cast<uint64_t>(tail[11]) << 24;
        case 11: k2 ^= static_cast<uint64_t>(tail[10]) << 16;
        case 10: k2 ^= static_cast<uint64_t>(tail[9]) << 8;
        case 9:  k2 ^= static_cast<uint64_t>(tail[8]);
                 k2 *= c2;
                 k2 = (k2 << 33) | (k2 >> 31);
                 k2 *= c1;
                 h2 ^= k2;
        case 8:  k1 ^= static_cast<uint64_t>(tail[7]) << 56;
        case 7:  k1 ^= static_cast<uint64_t>(tail[6]) << 48;
        case 6:  k1 ^= static_cast<uint64_t>(tail[5]) << 40;
        case 5:  k1 ^= static_cast<uint64_t>(tail[4]) << 32;
        case 4:  k1 ^= static_cast<uint64_t>(tail[3]) << 24;
        case 3:  k1 ^= static_cast<uint64_t>(tail[2]) << 16;
        case 2:  k1 ^= static_cast<uint64_t>(tail[1]) << 8;
        case 1:  k1 ^= static_cast<uint64_t>(tail[0]);
                 k1 *= c1;
                 k1 = (k1 << 31) | (k1 >> 33);
                 k1 *= c2;
                 h1 ^= k1;
    }
    
    h1 ^= len;
    h2 ^= len;
    
    h1 += h2;
    h2 += h1;
    
    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;
    h2 ^= h2 >> 33;
    h2 *= 0xc4ceb9fe1a85ec53ULL;
    h2 ^= h2 >> 33;
    
    h1 += h2;
    h2 += h1;
    
    out[0] = h1;
    out[1] = h2;
}

// DJB2 哈希
uint32_t DJB2Hash(const std::string& str) {
    uint32_t hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }
    return hash;
}

// SDBM 哈希
uint32_t SDBMHash(const std::string& str) {
    uint32_t hash = 0;
    for (char c : str) {
        hash = static_cast<uint32_t>(c) + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

// FNV-1a 32位哈希
uint32_t FNV1aHash32(const std::string& str) {
    const uint32_t FNV_offset_basis = 2166136261U;
    const uint32_t FNV_prime = 16777619U;
    
    uint32_t hash = FNV_offset_basis;
    for (char c : str) {
        hash ^= static_cast<uint32_t>(c);
        hash *= FNV_prime;
    }
    return hash;
}

// FNV-1a 64位哈希
uint64_t FNV1aHash64(const std::string& str) {
    const uint64_t FNV_offset_basis = 14695981039346656037ULL;
    const uint64_t FNV_prime = 1099511628211ULL;
    
    uint64_t hash = FNV_offset_basis;
    for (char c : str) {
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_prime;
    }
    return hash;
}

// CRC32 表
static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void InitCRC32Table() {
    if (crc32_initialized) return;
    
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ polynomial;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

uint32_t CRC32Hash(const std::string& str) {
    InitCRC32Table();
    
    uint32_t crc = 0xFFFFFFFF;
    for (char c : str) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ static_cast<uint8_t>(c)) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

// 简单哈希（用于教学）
uint32_t SimpleHash(const std::string& str) {
    uint32_t hash = 0;
    for (size_t i = 0; i < str.length(); i++) {
        hash = hash * 31 + static_cast<uint32_t>(str[i]);
    }
    return hash;
}

// 将哈希值映射到指定范围
uint32_t HashToRange(uint32_t hash, uint32_t range) {
    // 使用乘法哈希法
    return (static_cast<uint64_t>(hash) * range) >> 32;
}

// 一致性哈希
uint32_t ConsistentHash(const std::string& key, int virtual_node_count, uint32_t range) {
    uint32_t final_hash = 0;
    for (int i = 0; i < virtual_node_count; i++) {
        std::string virtual_key = key + "#" + std::to_string(i);
        uint32_t hash = MurmurHash3(virtual_key);
        if (hash > final_hash) {
            final_hash = hash;
        }
    }
    return HashToRange(final_hash, range);
}

// 布隆过滤器哈希
void BloomHash(const std::string& key, int k, uint64_t m, std::vector<uint64_t>& positions) {
    positions.clear();
    positions.reserve(k);
    
    // 使用双重哈希技术：h_i = h1 + i * h2
    uint64_t h1 = FNV1aHash64(key);
    uint64_t h2 = MurmurHash3(key, 0);
    if (h2 == 0) h2 = 1;  // 避免 h2 为 0
    
    for (int i = 0; i < k; i++) {
        uint64_t pos = (h1 + static_cast<uint64_t>(i) * h2) % m;
        positions.push_back(pos);
    }
}

// 字符串哈希（用于 unordered_map）
size_t StringHash::operator()(const std::string& str) const {
    return std::hash<std::string>{}(str);
}

// MD5 哈希（简化实现，返回十六进制字符串）
std::string MD5Hash(const std::string& str) {
    // 简化版本：实际项目中应使用 OpenSSL 或 mbedTLS
    // 这里返回一个基于 MurmurHash 的模拟值用于教学
    uint32_t h = MurmurHash3(str);
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x", 
                  h, h >> 16, h & 0xFFFF, ~h);
    return std::string(buf);
}

// SHA-1 哈希（简化实现，返回十六进制字符串）
std::string SHA1Hash(const std::string& str) {
    // 简化版本：实际项目中应使用 OpenSSL 或 mbedTLS
    // 这里返回一个基于 FNV-1a 的模拟值用于教学
    uint64_t h = FNV1aHash64(str);
    char buf[41];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx", 
                  (unsigned long long)h, (unsigned long long)(h >> 32));
    return std::string(buf);
}

} // namespace utils
} // namespace minisearchrec
