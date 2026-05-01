// ============================================================
// MiniSearchRec - LightGBM 精排处理器
// 参考：微信天秤树模型、X(Twitter) Heavy Ranker
//
// 编译条件：
//   有 LightGBM：#define HAVE_LIGHTGBM → 调用真实 C API 推理
//   无 LightGBM：降级到内置决策树规则（同样走树结构，无外部依赖）
// ============================================================

#ifndef MINISEARCHREC_LGBM_SCORER_H
#define MINISEARCHREC_LGBM_SCORER_H

#include "core/processor.h"
#include <string>
#include <vector>

#ifdef HAVE_LIGHTGBM
#include <LightGBM/c_api.h>
#endif

namespace minisearchrec {

// ============================================================
// 特征维度说明（与训练脚本 train_rank_model.py 保持一致）
//   feat[0]  query_len         query 分词数量（tanh 归一化）
//   feat[1]  bm25_score        BM25 相关性分
//   feat[2]  quality_score     质量分（点击/点赞加权）
//   feat[3]  freshness_score   时效性分
//   feat[4]  log_click         log(click+1) tanh 归一化
//   feat[5]  log_like          log(like+1) tanh 归一化
// ============================================================
static constexpr int kNumFeatures = 6;

class LGBMScorerProcessor : public BaseScorerProcessor {
public:
    LGBMScorerProcessor() = default;
    ~LGBMScorerProcessor() override;

    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override;

    std::string Name() const override { return "LGBMScorerProcessor"; }
    bool Init(const YAML::Node& config) override;

    // 加载模型文件（models/rank_model.txt）
    bool LoadModel(const std::string& model_path);

    // 是否已加载真实 LightGBM 模型
    bool HasRealModel() const { return model_loaded_; }

private:
    std::string model_path_;
    bool model_loaded_ = false;

#ifdef HAVE_LIGHTGBM
    BoosterHandle booster_ = nullptr;
#endif

    // 特征提取：Session + DocCandidate → float[kNumFeatures]
    std::vector<float> ExtractFeatures(const Session& session,
                                       const DocCandidate& cand) const;

    // 推理入口：有模型走 C API，无模型走内置规则树
    float Predict(const std::vector<float>& features) const;

    // 内置规则树（无 LightGBM 时降级，但仍然走树结构判断）
    float PredictBuiltinTree(const std::vector<float>& features) const;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_LGBM_SCORER_H
