// ============================================================
// MiniSearchRec - 排序元素基类
// 对标：通用搜索框架 RankItem
// ============================================================

#ifndef MINISEARCHREC_RANK_ITEM_H
#define MINISEARCHREC_RANK_ITEM_H

#include <string>
#include <set>
#include <memory>
#include <unordered_map>

namespace minisearchrec {
namespace rank {

// ============================================================
// BaseRankItem：排序队列中的元素基类
// 每个业务（Search/Sug/Hint/Nav）继承此类，添加自身字段
// ============================================================
struct BaseRankItem {
public:
    BaseRankItem() = default;
    virtual ~BaseRankItem() = default;

    // ── 分数 ──
    float Score() const { return score_; }
    void SetScore(float score) { score_ = score; }

    // ── 过滤 ──
    bool Filtered() const { return filtered_; }
    void SetFilter(bool filter, const std::string& reason = "") {
        filtered_ = filter;
        filter_reason_ = reason;
    }
    std::string FilterReason() const { return filter_reason_; }

    // ── 原始数据 ──
    const std::string& Word() const { return word_; }
    void SetWord(const std::string& w) { word_ = w; }

    const std::string& Desc() const { return desc_; }
    void SetDesc(const std::string& d) { desc_ = d; }

    uint32_t Type() const { return type_; }
    void SetType(uint32_t t) { type_ = t; }

    // ── 召回来源 ──
    void AddRetrieveType(uint32_t rt) { retrieve_types_.insert(rt); }
    bool HasRetrieveType(uint32_t rt) const {
        return retrieve_types_.find(rt) != retrieve_types_.end();
    }
    const std::set<uint32_t>& RetrieveTypes() const { return retrieve_types_; }

    // ── 自定义特征存储（kv 形式，业务可选用）──
    void SetFeature(const std::string& key, float val) { features_[key] = val; }
    float GetFeature(const std::string& key, float def = 0.0f) const {
        auto it = features_.find(key);
        return it != features_.end() ? it->second : def;
    }
    const std::unordered_map<std::string, float>& Features() const { return features_; }

    // ── Debug ──
    void AppendDebug(const std::string& tag) {
        if (!debug_info_.empty()) debug_info_ += "|";
        debug_info_ += tag;
    }
    const std::string& DebugInfo() const { return debug_info_; }

private:
    float score_ = 0.0f;
    bool filtered_ = false;
    std::string filter_reason_;

    std::string word_;
    std::string desc_;
    uint32_t type_ = 0;

    std::set<uint32_t> retrieve_types_;
    std::unordered_map<std::string, float> features_;
    std::string debug_info_;
};

using BaseRankItemPtr = std::shared_ptr<BaseRankItem>;

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_ITEM_H
