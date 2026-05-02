// ============================================================
// MiniSearchRec - 排序基类
// 对标：通用搜索框架 Rank
// Pipeline: PrepareInput → PreRank → DoRank (Processor链) → GenerateRankOutput
// ============================================================

#ifndef MINISEARCHREC_RANK_H
#define MINISEARCHREC_RANK_H

#include <memory>
#include <vector>
#include <string>
#include "lib/rank/base/rank_context.h"
#include "lib/rank/base/rank_factory.h"
#include "lib/rank/base/processor_interface.h"

namespace minisearchrec {
namespace rank {

class Rank {
public:
    Rank() = default;
    virtual ~Rank() = default;

    // 初始化：创建 Context + Vector，准备 Processor 链
    virtual int Init(const RankArgs& args);

    // 执行排序 Pipeline
    virtual int Process();

    // 获取上下文（Handler 用来读取结果）
    RankContextPtr GetContext() const { return ctx_; }

protected:
    // ── 可 override 的 Pipeline 步骤 ──
    virtual std::string RankName() const { return "Rank"; }
    virtual int PrepareInput() { return 0; }
    virtual int PreRank() { return 0; }
    virtual int DoRank();
    virtual int GenerateRankOutput() { return 0; }

    // ── Factory 和 Processor 获取（子类可 override）──
    virtual const RankFactory* GetFactory(const std::string& business_type);
    virtual std::vector<ProcessorConfig> GetProcessors(const std::string& business_type);

    RankContextPtr ctx_;
};

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_H
