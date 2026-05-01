// ============================================================
// MiniSearchRec - LightGBM 精排处理器实现
// 参考：微信天秤树模型、X(Twitter) Heavy Ranker
// ============================================================

#include "rank/lgbm_ranker.h"
#include "utils/logger.h"
#include <cmath>
#include <algorithm>
#include <fstream>

namespace minisearchrec {

// ============================================================
// LoadBoosterFromFile：从磁盘加载一个 Booster，封装为 BoosterPtr
// 只负责加载，不影响 active_booster_
// ============================================================
BoosterPtr LGBMScorerProcessor::LoadBoosterFromFile(
    const std::string& path) const
{
    std::ifstream f(path);
    if (!f.good()) {
        LOG_WARN("LGBMScorer: model file not found: {}", path);
        return nullptr;
    }
    f.close();

#ifdef HAVE_LIGHTGBM
    BoosterHandle raw = nullptr;
    int ret = LGBM_BoosterLoadModelFromFile(path.c_str(), &raw);
    if (ret != 0 || !raw) {
        LOG_ERROR("LGBMScorer: LGBM_BoosterLoadModelFromFile failed, path={}", path);
        return nullptr;
    }
    int num_iter = 0;
    LGBM_BoosterGetCurrentIteration(raw, &num_iter);
    LOG_INFO("LGBMScorer: loaded model from {} (iterations={})", path, num_iter);
    // BoosterDeleter 负责调用 LGBM_BoosterFree
    return BoosterPtr(raw, BoosterDeleter{});
#else
    LOG_WARN("LGBMScorer: compiled without LightGBM, cannot load {}", path);
    return nullptr;
#endif
}

// ============================================================
// Init：启动时调用，初始化权重并加载初始模型
// ============================================================
bool LGBMScorerProcessor::Init(const YAML::Node& config) {
    if (config["weight"]) {
        weight_ = config["weight"].as<float>(1.0f);
        weight_cached_ = weight_;
    }
    if (config["model_path"]) {
        std::string path = config["model_path"].as<std::string>("");
        if (!path.empty()) {
            bool ok = LoadModel(path);
            if (!ok) {
                LOG_INFO("LGBMScorer: no model loaded, using built-in rule tree");
            }
        }
    }
    return true;
}

// ============================================================
// LoadModel：启动时初始加载，设置 active_booster_
// ============================================================
bool LGBMScorerProcessor::LoadModel(const std::string& model_path) {
    BoosterPtr bp = LoadBoosterFromFile(model_path);
    if (!bp) return false;

    // 封装为 shared_ptr<BoosterPtr>，供原子操作
    auto new_slot = std::make_shared<BoosterPtr>(std::move(bp));
    std::atomic_store(&active_booster_, new_slot);

    {
        std::lock_guard<std::mutex> lk(meta_mutex_);
        model_path_ = model_path;
    }
    model_loaded_.store(true);
    LOG_INFO("LGBMScorer: initial model loaded from {}", model_path);
    return true;
}

// ============================================================
// HotReload：双 Buffer 热更新
//
// 流程：
//   1. reload_mutex_ 防止并发 reload 互相踩踏
//   2. 在 standby slot 加载新 Booster（此时推理线程仍用旧 active）
//   3. 加载成功后 atomic_store 原子替换 active_booster_
//   4. 旧 shared_ptr 引用计数归零时自动调用 BoosterDeleter 释放
//   推理线程在 Process() 开头 acquire active_booster_ 的 shared_ptr，
//   整个推理期间持有该引用，不受切换影响。
// ============================================================
bool LGBMScorerProcessor::HotReload(const std::string& new_model_path) {
    std::lock_guard<std::mutex> reload_lk(reload_mutex_);

    LOG_INFO("LGBMScorer: HotReload starting, new model={}", new_model_path);

    // Step 1: 在 standby 位置加载新模型（此步耗时，active 仍正常服务）
    BoosterPtr new_bp = LoadBoosterFromFile(new_model_path);
    if (!new_bp) {
        LOG_ERROR("LGBMScorer: HotReload failed, keeping old model");
        return false;
    }

    // Step 2: 原子替换 active_booster_
    //         旧的 shared_ptr<BoosterPtr> 引用计数 -1，
    //         若当前没有推理线程持有它，立即释放旧 Booster
    auto new_slot = std::make_shared<BoosterPtr>(std::move(new_bp));
    auto old_slot = std::atomic_exchange(&active_booster_, new_slot);

    {
        std::lock_guard<std::mutex> lk(meta_mutex_);
        model_path_ = new_model_path;
    }
    model_loaded_.store(true);

    // old_slot 在此处析构（若引用计数已为 0，BoosterDeleter 立即触发）
    LOG_INFO("LGBMScorer: HotReload complete, new model active, old model released");
    return true;
}

// ============================================================
// ExtractFeatures
// ============================================================
std::vector<float> LGBMScorerProcessor::ExtractFeatures(
    const Session& session,
    const DocCandidate& cand) const
{
    std::vector<float> feat(kNumFeatures, 0.0f);

    feat[0] = std::tanh(static_cast<float>(session.qp_info.terms.size()) / 5.0f);

    feat[1] = cand.debug_scores.count("bm25")
                  ? cand.debug_scores.at("bm25") : 0.0f;
    feat[2] = cand.debug_scores.count("quality")
                  ? cand.debug_scores.at("quality") : 0.0f;
    feat[3] = cand.debug_scores.count("freshness")
                  ? cand.debug_scores.at("freshness") : 0.0f;

    feat[4] = std::tanh(std::log1pf(static_cast<float>(cand.click_count)) / 5.0f);
    feat[5] = std::tanh(std::log1pf(static_cast<float>(cand.like_count))  / 5.0f);
    feat[6] = std::tanh(static_cast<float>(cand.title.size()) / 30.0f);

    {
        int match_count = 0;
        for (const auto& term : session.qp_info.terms) {
            if (!term.empty() &&
                (cand.title.find(term) != std::string::npos ||
                 cand.category.find(term) != std::string::npos)) {
                ++match_count;
            }
        }
        feat[7] = std::tanh(static_cast<float>(match_count) / 3.0f);
    }

    {
        float match = 0.0f;
        if (session.user_profile && !cand.category.empty()) {
            const auto& cat_weights = session.user_profile->category_weights();
            auto it = cat_weights.find(cand.category);
            if (it != cat_weights.end()) {
                match = std::min(1.0f, it->second * 2.0f);
            }
        }
        feat[8] = match;
    }

    {
        float src_id = 0.0f;
        if (cand.recall_source == "vector")        src_id = 1.0f / 3.0f;
        else if (cand.recall_source == "hot_content")   src_id = 2.0f / 3.0f;
        else if (cand.recall_source == "user_history")  src_id = 1.0f;
        feat[9] = src_id;
    }

    return feat;
}

// ============================================================
// PredictBuiltinTree：内置规则决策树降级
// ============================================================
float LGBMScorerProcessor::PredictBuiltinTree(
    const std::vector<float>& feat) const
{
    float bm25      = feat[1];
    float quality   = feat[2];
    float freshness = feat[3];
    float log_click = feat[4];
    float log_like  = feat[5];
    float tag_match = feat[7];
    float cat_match = feat[8];

    float t1 = 0.0f;
    if (bm25 > 0.5f || tag_match > 0.5f) {
        t1 = (quality > 0.3f) ? 0.8f : 0.5f;
    } else if (bm25 > 0.2f) {
        t1 = (freshness > 0.5f) ? 0.4f : 0.2f;
    } else {
        t1 = (log_click > 0.3f) ? 0.1f : -0.1f;
    }

    float t2 = 0.0f;
    if (quality > 0.6f) {
        t2 = (cat_match > 0.3f || log_like > 0.3f) ? 0.7f : 0.4f;
    } else if (quality > 0.3f) {
        t2 = (bm25 > 0.3f) ? 0.3f : 0.1f;
    } else {
        t2 = (freshness > 0.7f) ? 0.2f : -0.2f;
    }

    float t3 = 0.0f;
    if (log_click > 0.5f || log_like > 0.5f) {
        t3 = (bm25 > 0.1f) ? 0.6f : 0.3f;
    } else if (freshness > 0.8f) {
        t3 = 0.3f;
    } else {
        t3 = -0.1f;
    }

    float raw = (t1 + t2 + t3) / 3.0f;
    return (std::tanh(raw * 2.0f) + 1.0f) / 2.0f;
}

// ============================================================
// Predict：传入已 acquire 的 BoosterPtr，无锁推理
// ============================================================
float LGBMScorerProcessor::Predict(const BoosterPtr& booster,
                                    const std::vector<float>& feat) const {
#ifdef HAVE_LIGHTGBM
    if (booster && feat.size() == static_cast<size_t>(kNumFeatures)) {
        int64_t out_len = 0;
        double  out_result = 0.0;

        int ret = LGBM_BoosterPredictForMat(
            booster.get(),
            feat.data(),
            C_API_DTYPE_FLOAT32,
            /*nrow=*/1,
            /*ncol=*/kNumFeatures,
            /*is_row_major=*/1,
            C_API_PREDICT_NORMAL,
            /*start_iteration=*/0,
            /*num_iteration=*/-1,
            "",
            &out_len,
            &out_result
        );

        if (ret == 0 && out_len >= 1) {
            return static_cast<float>(out_result);
        }
        LOG_WARN("LGBMScorer: LGBM_BoosterPredictForMat failed, fallback to builtin");
    }
#else
    (void)booster;
#endif
    return PredictBuiltinTree(feat);
}

// ============================================================
// Process：对所有候选打精排分
// 关键：在循环开始前 acquire active_booster_ 一次，
//       整个批次使用同一个 Booster 实例，HotReload 发生时不影响本次推理。
// ============================================================
int LGBMScorerProcessor::Process(Session& session,
                                  std::vector<DocCandidate>& candidates) {
    if (candidates.empty()) return 0;

    // 一次性 acquire 当前活跃 Booster（引用计数 +1，保证本批推理安全）
    auto slot = std::atomic_load(&active_booster_);
    BoosterPtr booster = slot ? *slot : BoosterPtr{};

    for (auto& cand : candidates) {
        std::vector<float> feat = ExtractFeatures(session, cand);
        float score = Predict(booster, feat);
        cand.fine_score += score * weight_;
        cand.debug_scores["lgbm"] = score;
    }

    LOG_DEBUG("LGBMScorer: scored {} candidates (model_loaded={})",
              candidates.size(), model_loaded_.load());
    return 0;
}

} // namespace minisearchrec
