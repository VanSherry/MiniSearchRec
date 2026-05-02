// ============================================================
// MiniSearchRec - Sug Trie 前缀树实现
// ============================================================

#include "biz/sug/sug_trie.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>

namespace minisearchrec {

void SugTrie::Build(std::vector<TrieEntry> entries) {
    auto start = std::chrono::steady_clock::now();

    // 在新 buffer 中构建
    auto new_buffer = std::make_shared<TrieBuffer>();
    new_buffer->root = std::make_unique<TrieNode>();
    new_buffer->entries = std::move(entries);

    // 按 freq 降序排列（高频词优先存储，搜索时自然有序）
    std::sort(new_buffer->entries.begin(), new_buffer->entries.end(),
        [](const TrieEntry& a, const TrieEntry& b) {
            return a.freq > b.freq;
        });

    // 插入 Trie
    for (const auto& entry : new_buffer->entries) {
        if (entry.word.empty()) continue;
        InsertToTrie(new_buffer->root.get(), entry.word, &entry);
    }

    // 原子切换（读操作通过 shared_ptr 保证安全）
    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        active_buffer_ = new_buffer;
    }
    entries_count_.store(new_buffer->entries.size());

    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    LOG_INFO("SugTrie::Build: {} entries, cost={}ms", new_buffer->entries.size(), cost);
}

void SugTrie::InsertToTrie(TrieNode* root, const std::string& key, const TrieEntry* entry) {
    TrieNode* node = root;
    // UTF-8 逐字节插入（保持 UTF-8 序列完整性）
    for (size_t i = 0; i < key.size(); ++i) {
        char c = key[i];
        if (node->children.find(c) == node->children.end()) {
            node->children[c] = std::make_unique<TrieNode>();
        }
        node = node->children[c].get();
        // 每个前缀节点都存储该词条引用（用于前缀搜索）
        // 限制每个节点最多存 200 条避免内存爆炸
        if (node->entries.size() < 200) {
            node->entries.push_back(entry);
        }
    }
    node->is_end = true;
}

std::vector<const TrieEntry*> SugTrie::Search(const std::string& prefix, int limit) const {
    std::shared_ptr<TrieBuffer> buffer;
    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        buffer = active_buffer_;
    }

    if (!buffer || !buffer->root || prefix.empty()) {
        return {};
    }

    // 沿 prefix 找到对应节点
    const TrieNode* node = buffer->root.get();
    for (size_t i = 0; i < prefix.size(); ++i) {
        char c = prefix[i];
        auto it = node->children.find(c);
        if (it == node->children.end()) {
            return {};  // 无匹配
        }
        node = it->second.get();
    }

    // 该节点的 entries 已按 freq 降序排列
    std::vector<const TrieEntry*> result;
    result.reserve(std::min(limit, (int)node->entries.size()));
    for (int i = 0; i < limit && i < (int)node->entries.size(); ++i) {
        result.push_back(node->entries[i]);
    }

    return result;
}

} // namespace minisearchrec
