// ============================================================
// MiniSearchRec - Rank 基类实现
// ============================================================

#include "lib/rank/base/rank.h"
#include "lib/rank/base/rank_manager.h"
#include "utils/logger.h"
#include <chrono>

namespace minisearchrec {
namespace rank {

// RankFactory 默认创建 Rank 基类
Rank* RankFactory::CreateRank() const { return new Rank(); }

int Rank::Init(const RankArgs& args) {
    // 1. 获取 Factory
    const auto* factory = GetFactory(args.business_type);
    if (!factory) {
        LOG_ERROR("Rank::Init: no factory for business_type={}", args.business_type);
        return -1;
    }

    // 2. 创建 Context
    ctx_.reset(factory->CreateContext());
    if (!ctx_) {
        LOG_ERROR("Rank::Init: CreateContext failed");
        return -2;
    }
    if (ctx_->Init(args) != 0) {
        LOG_ERROR("Rank::Init: Context Init failed");
        return -3;
    }

    // 3. 创建 RankVector
    auto vec = std::shared_ptr<RankVector>(factory->CreateVector());
    ctx_->SetVector(vec);

    return 0;
}

int Rank::Process() {
    if (!ctx_) return -1;

    int ret = 0;

    // Step 1: PrepareInput
    {
        ret = PrepareInput();
        if (ret != 0) {
            LOG_ERROR("{}::PrepareInput failed, ret={}", RankName(), ret);
            return ret;
        }
    }

    // Step 2: PreRank
    {
        ret = PreRank();
        if (ret != 0) {
            LOG_ERROR("{}::PreRank failed, ret={}", RankName(), ret);
            return ret;
        }
    }

    // Step 3: DoRank (执行 Processor 链)
    {
        ret = DoRank();
        if (ret != 0) {
            LOG_ERROR("{}::DoRank failed, ret={}", RankName(), ret);
            return ret;
        }
    }

    // Step 4: GenerateRankOutput
    {
        ret = GenerateRankOutput();
        if (ret != 0) {
            LOG_ERROR("{}::GenerateRankOutput failed, ret={}", RankName(), ret);
            return ret;
        }
    }

    LOG_INFO("{}::Process complete, business={}, items={}, cost={}ms",
             RankName(), ctx_->BusinessType(), ctx_->GetVector()->Size(), ctx_->ElapsedMs());
    return 0;
}

int Rank::DoRank() {
    auto processors_cfg = GetProcessors(ctx_->BusinessType());
    auto vec = ctx_->GetVector();

    LOG_INFO("{}::DoRank: {} processors, {} items before rank",
             RankName(), processors_cfg.size(), vec->Size());

    for (const auto& proc_cfg : processors_cfg) {
        // 创建 Processor 实例
        auto* raw_ptr = ProcessorRegistry::Instance().Create(proc_cfg.name);
        if (!raw_ptr) {
            LOG_WARN("{}::DoRank: processor '{}' not found, skip", RankName(), proc_cfg.name);
            continue;
        }
        ProcessorInterfacePtr processor(raw_ptr);

        // Init + Process
        auto start = std::chrono::steady_clock::now();
        if (processor->Init(ctx_, &proc_cfg) != 0) {
            LOG_WARN("{}::DoRank: processor '{}' init failed, skip", RankName(), proc_cfg.name);
            continue;
        }

        int ret = processor->Process();
        auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (ret != 0) {
            LOG_ERROR("{}::DoRank: processor '{}' failed, ret={}", RankName(), proc_cfg.name, ret);
            return ret;
        }

        LOG_INFO("  [{}] cost={}us, items_after={}", proc_cfg.name, cost_us, vec->Size());
    }

    return 0;
}

const RankFactory* Rank::GetFactory(const std::string& business_type) {
    return RankManager::Instance().GetFactory(business_type);
}

std::vector<ProcessorConfig> Rank::GetProcessors(const std::string& business_type) {
    return RankManager::Instance().GetProcessors(business_type);
}

} // namespace rank
} // namespace minisearchrec
