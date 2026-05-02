// ============================================================
// MiniSearchRec - 排序上下文基类
// 对标：通用搜索框架 RankContext
// 承载请求信息、排序队列、业务上下文
// ============================================================

#ifndef MINISEARCHREC_RANK_CONTEXT_H
#define MINISEARCHREC_RANK_CONTEXT_H

#include <string>
#include <memory>
#include <chrono>
#include "lib/rank/base/rank_vector.h"

namespace minisearchrec {
namespace rank {

// ============================================================
// RankArgs：排序入参（由 Handler 构造后传给 Rank）
// ============================================================
struct RankArgs {
    std::string uid;
    std::string query;
    std::string business_type;  // "search" / "sug" / "hint" / "nav"
    int page_size = 10;

    // 扩展字段（业务自定义）
    std::string doc_id;         // hint 用
    std::string customize;      // 序列化的自定义数据
};

// ============================================================
// RankContext：排序上下文
// 每个业务继承此类添加自身上下文（如 SugContext、HintContext）
// ============================================================
class RankContext {
public:
    RankContext() = default;
    virtual ~RankContext() = default;

    virtual int Init(const RankArgs& args) {
        uid_ = args.uid;
        query_ = args.query;
        business_type_ = args.business_type;
        page_size_ = args.page_size;
        start_time_ = std::chrono::steady_clock::now();
        return 0;
    }

    // ── 基础访问器 ──
    const std::string& Uid() const { return uid_; }
    const std::string& Query() const { return query_; }
    const std::string& BusinessType() const { return business_type_; }
    int PageSize() const { return page_size_; }

    // ── 排序队列（由 Rank::Init 创建后设置）──
    RankVectorPtr GetVector() const { return vec_ptr_; }
    void SetVector(RankVectorPtr v) { vec_ptr_ = std::move(v); }

    // ── 耗时 ──
    int64_t ElapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time_).count();
    }

protected:
    std::string uid_;
    std::string query_;
    std::string business_type_;
    int page_size_ = 10;

    RankVectorPtr vec_ptr_;
    std::chrono::steady_clock::time_point start_time_;
};

using RankContextPtr = std::shared_ptr<RankContext>;

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_CONTEXT_H
