// ============================================================
// MiniSearchRec - LightGBM 精排处理器
// 参考：微信天秤树模型、X(Twitter) Heavy Ranker
//
// 编译条件：
//   有 LightGBM：#define HAVE_LIGHTGBM → 调用真实 C API 推理
//   无 LightGBM：降级到内置决策树规则（同样走树结构，无外部依赖）
//
// 热更新设计（双 Buffer）：
//   - active_booster_   当前推理使用的 Booster（原子指针，无锁读）
//   - standby_booster_  HotReload 时在后台加载的新 Booster
//   - 加载完成后原子交换 active/standby，旧 Booster 在 standby 位置等待析构
//   - 推理线程持有 shared_ptr 引用计数，旧 Booster 在最后一个推理完成后才释放
// ============================================================

#ifndef MINISEARCHREC_LGBM_SCORER_H
#define MINISEARCHREC_LGBM_SCORER_H

#include "core/processor.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>

#ifdef HAVE_LIGHTGBM
#include <LightGBM/c_api.h>
#endif

namespace minisearchrec {

// ============================================================
// 特征维度说明（与训练脚本 train_rank_model.py 保持一致）
//   feat[0]  query_len           query 分词数量（tanh 归一化）
//   feat[1]  bm25_score          BM25 相关性分
//   feat[2]  quality_score       质量分（点击/点赞加权）
//   feat[3]  freshness_score     时效性分（指数衰减）
//   feat[4]  log_click           log(click+1) tanh 归一化
//   feat[5]  log_like            log(like+1) tanh 归一化
//   feat[6]  title_len           标题长度（tanh 归一化）
//   feat[7]  tag_match_count     标题/类别命中 query 词数（tanh 归一化）
//   feat[8]  category_match      用户兴趣类别与文章类别是否匹配（0/1）
//   feat[9]  recall_source_id    召回来源编号（inverted=0/vector=1/hot=2/history=3）
// ============================================================
static constexpr int kNumFeatures = 10;

// ============================================================
// BoosterHandle 的 RAII 包装（用于 shared_ptr 自动释放）
// ============================================================
#ifdef HAVE_LIGHTGBM
struct BoosterDeleter {
    void operator()(void* p) const {
        if (p) LGBM_BoosterFree(p);
    }
};
using BoosterPtr = std::shared_ptr<void>;  // void* = BoosterHandle
#else
using BoosterPtr = std::shared_ptr<void>;  // 无 LightGBM 时只是占位
#endif

class LGBMScorerProcessor : public BaseScorerProcessor {
public:
    LGBMScorerProcessor() = default;
    ~LGBMScorerProcessor() override = default;  // shared_ptr 自动释放 Booster

    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override;

    std::string Name() const override { return "LGBMScorerProcessor"; }
    bool Init(const YAML::Node& config) override;

    // 初始加载模型（启动时调用）
    bool LoadModel(const std::string& model_path);

    // 热更新：后台加载新模型，加载完成后原子切换 active buffer
    // 线程安全，推理线程不受影响（推理期间持有旧 BoosterPtr 引用）
    // 返回：true=切换成功，false=新模型加载失败（保持旧模型）
    bool HotReload(const std::string& new_model_path);

    // 当前使用的模型路径
    std::string CurrentModelPath() const {
        std::lock_guard<std::mutex> lk(meta_mutex_);
        return model_path_;
    }

    bool HasRealModel() const { return model_loaded_.load(); }

private:
    // ── 双 Buffer 核心 ──
    // 推理时 acquire active_booster_（shared_ptr，引用计数保证安全）
    // HotReload 时只写 standby，切换完成后 swap active/standby
    mutable std::mutex      reload_mutex_;   // 保护 HotReload 并发
    mutable std::mutex      meta_mutex_;     // 保护 model_path_ 等元信息
    std::shared_ptr<BoosterPtr> active_booster_;   // 原子可见的活跃 Booster
    // 使用 shared_ptr<BoosterPtr> 包一层，atomic_load/atomic_store 操作

    std::atomic<bool> model_loaded_{false};
    std::string       model_path_;
    float             weight_cached_ = 1.0f;

    // 从文件加载一个新 BoosterPtr（不影响 active_booster_）
    BoosterPtr LoadBoosterFromFile(const std::string& path) const;

    std::vector<float> ExtractFeatures(const Session& session,
                                       const DocCandidate& cand) const;

    float Predict(const BoosterPtr& booster,
                  const std::vector<float>& features) const;

    float PredictBuiltinTree(const std::vector<float>& features) const;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_LGBM_SCORER_H
