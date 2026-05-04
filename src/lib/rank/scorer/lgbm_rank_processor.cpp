#include "lib/rank/scorer/lgbm_rank_processor.h"
#include "utils/logger.h"
#include <cmath>
#include <fstream>

namespace minisearchrec {

// ── 静态成员 ──
std::shared_ptr<BoosterPtr> LGBMRankProcessor::s_booster;
std::atomic<bool> LGBMRankProcessor::s_loaded{false};
std::mutex LGBMRankProcessor::s_reload_mutex;
std::string LGBMRankProcessor::s_model_path;

// ============================================================
// 模型管理
// ============================================================
static BoosterPtr LoadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) { LOG_WARN("LGBMRank: model file not found: {}", path); return nullptr; }
    f.close();
#ifdef HAVE_LIGHTGBM
    void* raw = nullptr;
    if (LGBM_BoosterLoadModelFromFile(path.c_str(), &raw) != 0 || !raw) {
        LOG_ERROR("LGBMRank: LoadModelFromFile failed: {}", path);
        return nullptr;
    }
    int it = 0; LGBM_BoosterGetCurrentIteration(raw, &it);
    LOG_INFO("LGBMRank: loaded model from {} (iter={})", path, it);
    return BoosterPtr(raw, BoosterDeleter{});
#else
    (void)path;
    return nullptr;
#endif
}

bool LGBMRankProcessor::LoadModel(const std::string& path) {
    auto bp = LoadFromFile(path);
    if (!bp && !path.empty()) return false;
    auto new_slot = std::make_shared<BoosterPtr>(std::move(bp));
    s_booster = new_slot;
    { std::lock_guard<std::mutex> lk(s_reload_mutex); s_model_path = path; }
    s_loaded.store(true);
    LOG_INFO("LGBMRankProcessor: model loaded: {}", path);
    return true;
}

bool LGBMRankProcessor::HasModel() { return s_loaded.load(); }

bool LGBMRankProcessor::HotReload(const std::string& new_path) {
    std::lock_guard<std::mutex> lk(s_reload_mutex);
    auto bp = LoadFromFile(new_path);
    if (!bp) { LOG_ERROR("LGBMRank: HotReload failed"); return false; }
    s_booster = std::make_shared<BoosterPtr>(std::move(bp));
    s_model_path = new_path;
    LOG_INFO("LGBMRankProcessor: HotReload complete: {}", new_path);
    return true;
}

// ============================================================
// 内置决策树降级
// ============================================================
static float PredictBuiltin(const float* feat) {
    auto t = [](bool c, float a, float b) { return c ? a : b; };
    float t1 = feat[1] > 0.5f || feat[7] > 0.5f ? t(feat[2] > 0.3f, 0.8f, 0.5f)
              : feat[1] > 0.2f ? t(feat[3] > 0.5f, 0.4f, 0.2f)
              : t(feat[4] > 0.3f, 0.1f, -0.1f);
    float t2 = feat[2] > 0.6f ? t(feat[8] > 0.3f || feat[5] > 0.3f, 0.7f, 0.4f)
              : feat[2] > 0.3f ? t(feat[1] > 0.3f, 0.3f, 0.1f)
              : t(feat[3] > 0.7f, 0.2f, -0.2f);
    float t3 = feat[4] > 0.5f || feat[5] > 0.5f ? t(feat[1] > 0.1f, 0.6f, 0.3f)
              : feat[3] > 0.8f ? 0.3f : -0.1f;
    return (std::tanh(((t1 + t2 + t3) / 3.0f) * 2.0f) + 1.0f) / 2.0f;
}

// ============================================================
// Process
// ============================================================
int LGBMRankProcessor::Process() {
    if (!ctx_) return -1;
    float w = config_ ? config_->weight : 1.0f;

    auto booster_slot = s_booster;
    BoosterPtr booster = booster_slot ? *booster_slot : BoosterPtr{};

    auto vec = ctx_->GetVector();
    static constexpr int kNF = 10;

    for (uint32_t i = 0; i < vec->Size(); ++i) {
        auto* item = vec->GetItem(i).get();
        if (!item) continue;

        // 全部从 KV 特征读取，0 依赖任何业务类型
        float feat[kNF] = {
            item->GetFeature("query_len"),
            item->GetFeature("bm25"),
            item->GetFeature("quality"),
            item->GetFeature("freshness"),
            item->GetFeature("log_click"),
            item->GetFeature("log_like"),
            item->GetFeature("title_len"),
            item->GetFeature("tag_match"),
            item->GetFeature("cat_match"),
            item->GetFeature("recall_source_id"),
        };

        float score = PredictBuiltin(feat);
#ifdef HAVE_LIGHTGBM
        if (booster) {
            double out = 0; int64_t out_len = 0;
            if (LGBM_BoosterPredictForMat(booster.get(), feat, C_API_DTYPE_FLOAT32,
                    1, kNF, 1, C_API_PREDICT_NORMAL, 0, -1, "", &out_len, &out) == 0 && out_len >= 1)
                score = (float)out;
        }
#endif
        item->SetFeature("lgbm_score", score * w);
    }

    LOG_DEBUG("LGBMRankProcessor: scored {} items (weight={:.2f}, model_loaded={})",
              vec->Size(), w, s_loaded.load());
    return 0;
}

REGISTER_RANK_PROCESSOR(LGBMRankProcessor);
} // namespace minisearchrec
