// ============================================================
// MiniSearchRec - 排序队列
// 对标：通用搜索框架 RankVector
// 管理活跃 item 列表 + 被过滤 item 列表
// ============================================================

#ifndef MINISEARCHREC_RANK_VECTOR_H
#define MINISEARCHREC_RANK_VECTOR_H

#include <vector>
#include <algorithm>
#include <functional>
#include <string>
#include "lib/rank/base/rank_item.h"

namespace minisearchrec {
namespace rank {

class RankVector {
public:
    RankVector() = default;
    virtual ~RankVector() = default;

    // ── 基础操作 ──
    uint32_t Size() const { return static_cast<uint32_t>(items_.size()); }
    bool Empty() const { return items_.empty(); }

    BaseRankItemPtr GetItem(uint32_t pos) const {
        if (pos >= items_.size()) return nullptr;
        return items_[pos];
    }

    void PushBack(BaseRankItemPtr item) { items_.push_back(std::move(item)); }
    void Clear() { items_.clear(); }

    // ── 排序 ──
    void SortByScore() {
        std::stable_sort(items_.begin(), items_.end(),
            [](const BaseRankItemPtr& a, const BaseRankItemPtr& b) {
                return a->Score() > b->Score();
            });
    }

    template <typename Compare>
    void Sort(Compare cmp) {
        std::stable_sort(items_.begin(), items_.end(), cmp);
    }

    template <typename Compare>
    void Sort(uint32_t begin, uint32_t end, Compare cmp) {
        if (begin >= end || end > items_.size()) return;
        std::stable_sort(items_.begin() + begin, items_.begin() + end, cmp);
    }

    // ── 过滤：将标记为 filtered 的 item 移到 filtered 列表 ──
    bool FilterItems() {
        std::vector<BaseRankItemPtr> active;
        for (auto& item : items_) {
            if (item->Filtered()) {
                filtered_items_.push_back(std::move(item));
            } else {
                active.push_back(std::move(item));
            }
        }
        items_ = std::move(active);
        return true;
    }

    // 标记某个位置的 item 为过滤并立即移入 filtered 列表
    bool SetItemFilter(uint32_t pos, const std::string& reason = "") {
        if (pos >= items_.size()) return false;
        items_[pos]->SetFilter(true, reason);
        filtered_items_.push_back(items_[pos]);
        items_.erase(items_.begin() + pos);
        return true;
    }

    // ── 截断 ──
    void Truncate(uint32_t max_size) {
        if (items_.size() > max_size) {
            items_.resize(max_size);
        }
    }

    // ── 访问过滤列表 ──
    const std::vector<BaseRankItemPtr>& GetFilteredItems() const { return filtered_items_; }

    // ── 迭代器 ──
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }

    // ── Debug ──
    std::string DebugString() const {
        std::string s = "RankVector[" + std::to_string(items_.size()) + " active, "
                      + std::to_string(filtered_items_.size()) + " filtered]: ";
        for (uint32_t i = 0; i < std::min((uint32_t)5, Size()); ++i) {
            s += items_[i]->Word() + "(" + std::to_string(items_[i]->Score()) + ") ";
        }
        if (Size() > 5) s += "...";
        return s;
    }

private:
    std::vector<BaseRankItemPtr> items_;
    std::vector<BaseRankItemPtr> filtered_items_;
};

using RankVectorPtr = std::shared_ptr<RankVector>;

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_VECTOR_H
