// ============================================================
// MiniSearchRec - Sug Trie 前缀树
// 对标：通用搜索框架/business/suggester/indexer/sug_trie.h
// 支持中文 UTF-8 前缀匹配，定时从词典文件重建
// ============================================================

#ifndef MINISEARCHREC_SUG_TRIE_H
#define MINISEARCHREC_SUG_TRIE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

namespace minisearchrec {

// Trie 中存储的词条
struct TrieEntry {
    std::string word;
    std::string source;    // title / tag / user_query
    int64_t     freq = 0;
    int64_t     last_time = 0;
    float       source_weight = 1.0f;
};

// Trie 节点
struct TrieNode {
    std::unordered_map<char, std::unique_ptr<TrieNode>> children;
    std::vector<const TrieEntry*> entries;  // 指向到此前缀的所有词条
    bool is_end = false;
};

// ============================================================
// SugTrie：线程安全的 Trie 前缀树（双 Buffer 支持热更新）
// ============================================================
class SugTrie {
public:
    static SugTrie& Instance() {
        static SugTrie inst;
        return inst;
    }

    // 从词条列表构建 Trie（全量重建）
    // entries 的生命周期由 SugTrie 内部管理
    void Build(std::vector<TrieEntry> entries);

    // 前缀搜索：返回匹配的词条列表（按 freq 降序，限制 limit 条）
    std::vector<const TrieEntry*> Search(const std::string& prefix, int limit = 50) const;

    // 获取当前词条总数
    size_t Size() const { return entries_count_.load(); }

    // 重建标记（由后台线程定时触发）
    bool NeedRebuild() const { return need_rebuild_.load(); }
    void MarkNeedRebuild() { need_rebuild_.store(true); }
    void ClearRebuildMark() { need_rebuild_.store(false); }

private:
    SugTrie() = default;

    void InsertToTrie(TrieNode* root, const std::string& key, const TrieEntry* entry);
    void CollectEntries(const TrieNode* node, std::vector<const TrieEntry*>& result, int limit) const;

    // 双 Buffer：active 供读，building 供写，写完原子切换
    struct TrieBuffer {
        std::unique_ptr<TrieNode> root;
        std::vector<TrieEntry> entries;  // 持有数据所有权
    };

    mutable std::mutex read_mutex_;
    std::shared_ptr<TrieBuffer> active_buffer_;
    std::atomic<size_t> entries_count_{0};
    std::atomic<bool> need_rebuild_{false};
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SUG_TRIE_H
