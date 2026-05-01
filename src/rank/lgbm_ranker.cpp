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
// 析构：释放 LightGBM booster
// ============================================================
LGBMScorerProcessor::~LGBMScorerProcessor() {
#ifdef HAVE_LIGHTGBM
    if (booster_) {
        LGBM_BoosterFree(booster_);
        booster_ = nullptr;
    }
#endif
}

// ============================================================
// Init：从 YAML 配置读取权重/模型路径
// ============================================================
bool LGBMScorerProcessor::Init(const YAML::Node& config) {
    if (config["weight"]) {
        weight_ = config["weight"].as<float>(1.0f);
    }
    if (config["model_path"]) {
        model_path_ = config["model_path"].as<std::string>("");
        if (!model_path_.empty()) {
            model_loaded_ = LoadModel(model_path_);
        }
    }
    if (!model_loaded_) {
        LOG_INFO("LGBMScorer: no model loaded, using built-in rule tree (降级模式)");
    }
    return true;
}

// ============================================================
// LoadModel：加载 LightGBM 文本格式模型
// ============================================================
bool LGBMScorerProcessor::LoadModel(const std::string& model_path) {
    // 检查文件是否存在
    std::ifstream f(model_path);
    if (!f.good()) {
        LOG_WARN("LGBMScorer: model file not found: {}", model_path);
        return false;
    }
    f.close();

#ifdef HAVE_LIGHTGBM
    // 释放旧 booster
    if (booster_) {
        LGBM_BoosterFree(booster_);
        booster_ = nullptr;
    }

    int ret = LGBM_BoosterLoadModelFromFile(model_path.c_str(), &booster_);
    if (ret != 0) {
        LOG_ERROR("LGBMScorer: LGBM_BoosterLoadModelFromFile failed, path={}", model_path);
        booster_ = nullptr;
        return false;
    }

    // 获取已加载的迭代数（树数量）
    int num_iter = 0;
    LGBM_BoosterGetCurrentIteration(booster_, &num_iter);
    LOG_INFO("LGBMScorer: loaded LightGBM model from {} (iterations={})",
             model_path, num_iter);
    return true;

#else
    // 无 LightGBM 编译时：检测到文件存在，但无法加载
    LOG_WARN("LGBMScorer: compiled without LightGBM, cannot load model from {}. "
             "Using built-in rule tree.", model_path);
    return false;
#endif
}

// ============================================================
// ExtractFeatures：提取 kNumFeatures 维特征
// 特征顺序必须与 train_rank_model.py 保持一致
// ============================================================
std::vector<float> LGBMScorerProcessor::ExtractFeatures(
    const Session& session,
    const DocCandidate& cand) const
{
    std::vector<float> feat(kNumFeatures, 0.0f);

    // feat[0]: query 分词数量（tanh 归一化，防止超长 query 影响）
    feat[0] = std::tanh(static_cast<float>(session.qp_info.terms.size()) / 5.0f);

    // feat[1]: BM25 相关性分（粗排已计算，直接取）
    feat[1] = cand.debug_scores.count("bm25")
                  ? cand.debug_scores.at("bm25") : 0.0f;

    // feat[2]: 质量分
    feat[2] = cand.debug_scores.count("quality")
                  ? cand.debug_scores.at("quality") : 0.0f;

    // feat[3]: 时效性分
    feat[3] = cand.debug_scores.count("freshness")
                  ? cand.debug_scores.at("freshness") : 0.0f;

    // feat[4]: 点击数（log 平滑 + tanh 归一化）
    feat[4] = std::tanh(std::log1pf(static_cast<float>(cand.click_count)) / 5.0f);

    // feat[5]: 点赞数（log 平滑 + tanh 归一化）
    feat[5] = std::tanh(std::log1pf(static_cast<float>(cand.like_count)) / 5.0f);

    // feat[6]: 标题长度（UTF-8 字节数，tanh 归一化，正常标题 10-60 bytes）
    feat[6] = std::tanh(static_cast<float>(cand.title.size()) / 30.0f);

    // feat[7]: 标题/类别命中 query 词数（tanh 归一化）
    {
        int match_count = 0;
        for (const auto& term : session.qp_info.terms) {
            if (!term.empty()) {
                if (cand.title.find(term) != std::string::npos ||
                    cand.category.find(term) != std::string::npos) {
                    ++match_count;
                }
            }
        }
        feat[7] = std::tanh(static_cast<float>(match_count) / 3.0f);
    }

    // feat[8]: 用户兴趣类别与文章类别匹配（0/1）
    {
        float match = 0.0f;
        if (session.user_profile && !cand.category.empty()) {
            const auto& cat_weights = session.user_profile->category_weights();
            auto it = cat_weights.find(cand.category);
            if (it != cat_weights.end()) {
                match = std::min(1.0f, it->second * 2.0f); // 线性映射到 [0,1]
            }
        }
        feat[8] = match;
    }

    // feat[9]: 召回来源编号（离散数值，归一化到 [0,1]）
    {
        float src_id = 0.0f;
        if (cand.recall_source == "vector")       src_id = 1.0f / 3.0f;
        else if (cand.recall_source == "hot_content") src_id = 2.0f / 3.0f;
        else if (cand.recall_source == "user_history") src_id = 1.0f;
        feat[9] = src_id;
    }

    return feat;
}

// ============================================================
// PredictBuiltinTree：内置规则决策树（无 LightGBM 时降级）
// 树逻辑：BM25 高 + quality 高 + freshness 高 → 高分
// 共 3 棵树，模拟 ensemble，每棵树叶节点值在 [-1, 1]
// ============================================================
float LGBMScorerProcessor::PredictBuiltinTree(
    const std::vector<float>& feat) const
{
    // feat 已经是 tanh 归一化后的值，在 [0,1] 左右
    float bm25         = feat[1];
    float quality      = feat[2];
    float freshness    = feat[3];
    float log_click    = feat[4];
    float log_like     = feat[5];
    float tag_match    = feat[7];
    float cat_match    = feat[8];

    // Tree 1：基于 BM25 + 标签匹配分裂
    float t1 = 0.0f;
    if (bm25 > 0.5f || tag_match > 0.5f) {
        t1 = (quality > 0.3f) ? 0.8f : 0.5f;
    } else if (bm25 > 0.2f) {
        t1 = (freshness > 0.5f) ? 0.4f : 0.2f;
    } else {
        t1 = (log_click > 0.3f) ? 0.1f : -0.1f;
    }

    // Tree 2：基于 quality + 用户兴趣类别匹配分裂
    float t2 = 0.0f;
    if (quality > 0.6f) {
        t2 = (cat_match > 0.3f || log_like > 0.3f) ? 0.7f : 0.4f;
    } else if (quality > 0.3f) {
        t2 = (bm25 > 0.3f) ? 0.3f : 0.1f;
    } else {
        t2 = (freshness > 0.7f) ? 0.2f : -0.2f;
    }

    // Tree 3：基于点击行为分裂
    float t3 = 0.0f;
    if (log_click > 0.5f || log_like > 0.5f) {
        t3 = (bm25 > 0.1f) ? 0.6f : 0.3f;
    } else if (freshness > 0.8f) {
        t3 = 0.3f;
    } else {
        t3 = -0.1f;
    }

    // 三棵树平均，压缩到 (0, 1)
    float raw = (t1 + t2 + t3) / 3.0f;
    return (std::tanh(raw * 2.0f) + 1.0f) / 2.0f;  // → [0, 1]
}

// ============================================================
// Predict：推理入口
// ============================================================
float LGBMScorerProcessor::Predict(const std::vector<float>& feat) const {
#ifdef HAVE_LIGHTGBM
    if (booster_ && feat.size() == static_cast<size_t>(kNumFeatures)) {
        int64_t out_len = 0;
        double out_result = 0.0;

        // 单样本推理：1 行 × kNumFeatures 列
        int ret = LGBM_BoosterPredictForMat(
            booster_,
            feat.data(),
            C_API_DTYPE_FLOAT32,
            /*nrow=*/1,
            /*ncol=*/kNumFeatures,
            /*is_row_major=*/1,
            C_API_PREDICT_NORMAL,
            /*start_iteration=*/0,
            /*num_iteration=*/-1,  // 使用全部迭代
            "",
            &out_len,
            &out_result
        );

        if (ret == 0 && out_len >= 1) {
            // sigmoid 输出已在 [0,1]（binary/lambdarank）
            return static_cast<float>(out_result);
        }
        LOG_WARN("LGBMScorer: LGBM_BoosterPredictForMat failed, fallback to builtin");
    }
#endif
    return PredictBuiltinTree(feat);
}

// ============================================================
// Process：对所有候选打精排分
// ============================================================
int LGBMScorerProcessor::Process(Session& session,
                                  std::vector<DocCandidate>& candidates) {
    if (candidates.empty()) return 0;

    for (auto& cand : candidates) {
        std::vector<float> feat = ExtractFeatures(session, cand);
        float score = Predict(feat);
        cand.fine_score += score * weight_;
        cand.debug_scores["lgbm"] = score;
    }

    LOG_DEBUG("LGBMScorer: scored {} candidates (model_loaded={})",
              candidates.size(), model_loaded_);
    return 0;
}

} // namespace minisearchrec
