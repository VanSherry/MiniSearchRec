#ifndef MINISEARCHREC_LGBM_RANK_PROCESSOR_H
#define MINISEARCHREC_LGBM_RANK_PROCESSOR_H

#include "lib/rank/engine/rank_engine.h"
#include <memory>
#include <atomic>
#include <mutex>

namespace minisearchrec {

using BoosterDeleter = void(*)(void*);
using BoosterPtr = std::unique_ptr<void, BoosterDeleter>;

class LGBMRankProcessor : public rank::ProcessorInterface {
public:
    int Init(const rank::ProcessorConfig* config) override;
    int Process(std::vector<rank::RankItem>& items) override;
    std::string Name() const override { return "LGBMRankProcessor"; }

    // 模型管理（静态，线程安全）
    static bool LoadModel(const std::string& path);
    static bool HasModel();
    static bool HotReload(const std::string& new_path);

private:
    static float PredictBuiltin(const float* feat);

    // 全局模型
    static std::shared_ptr<BoosterPtr> s_booster;
    static std::atomic<bool> s_loaded;
    static std::mutex s_reload_mutex;
    static std::string s_model_path;
};

} // namespace minisearchrec

#endif
