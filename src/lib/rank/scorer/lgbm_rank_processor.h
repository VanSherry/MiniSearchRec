#ifndef MINISEARCHREC_LGBM_RANK_PROCESSOR_H
#define MINISEARCHREC_LGBM_RANK_PROCESSOR_H

#include "lib/rank/base/processor_interface.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#ifdef HAVE_LIGHTGBM
#include <LightGBM/c_api.h>
struct BoosterDeleter { void operator()(void* p) const { if (p) LGBM_BoosterFree(p); } };
using BoosterPtr = std::shared_ptr<void>;
#else
using BoosterPtr = std::shared_ptr<void>;
#endif

namespace minisearchrec {

// LGBM 精排 Processor（通用 KV 特征版）
// 读取 PrepareInput 预计算的 10 维特征，输出 "lgbm_score"
// 不依赖任何业务类型
class LGBMRankProcessor : public rank::ProcessorInterface {
public:
    int Process() override;
    std::string Name() const override { return "LGBMRankProcessor"; }

    // ── 模型管理（静态，线程安全）──
    static bool LoadModel(const std::string& path);
    static bool HasModel();
    static bool HotReload(const std::string& new_path);

private:
    // 全局模型
    static std::shared_ptr<BoosterPtr> s_booster;
    static std::atomic<bool> s_loaded;
    static std::mutex s_reload_mutex;
    static std::string s_model_path;
};

} // namespace minisearchrec

#endif
