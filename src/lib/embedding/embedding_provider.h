// ============================================================
// MiniSearchRec - Embedding 提供器抽象层
// 通过配置一键切换 pseudo / onnx 两种模式
//
// 架构：
//   EmbeddingProvider (基类)
//   ├── PseudoEmbeddingProvider   词袋哈希（零依赖降级方案）
//   └── OnnxEmbeddingProvider     内置 ONNX 模型（bge-base-zh-v1.5）
//
// 已废弃：HttpEmbeddingProvider（popen+curl 实现，有安全隐患）
//         内置 ONNX 模型后不再需要远程调用
// ============================================================

#ifndef MINISEARCHREC_EMBEDDING_PROVIDER_H
#define MINISEARCHREC_EMBEDDING_PROVIDER_H

#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <functional>
#include <yaml-cpp/yaml.h>

namespace minisearchrec {

// ============================================================
// EmbeddingProvider 基类（策略模式）
// ============================================================
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    // 对文本生成 embedding 向量
    virtual std::vector<float> Encode(const std::string& text) = 0;

    // 批量编码（默认逐条调用，子类可覆盖为批量推理）
    virtual std::vector<std::vector<float>> EncodeBatch(
        const std::vector<std::string>& texts) {
        std::vector<std::vector<float>> results;
        results.reserve(texts.size());
        for (const auto& t : texts) {
            results.push_back(Encode(t));
        }
        return results;
    }

    // 获取输出维度
    virtual int GetDim() const = 0;

    // 提供器名称（用于日志）
    virtual std::string Name() const = 0;
};

// ============================================================
// PseudoEmbeddingProvider - 词袋哈希投影（零依赖降级方案）
// 当无 ONNX Runtime / 无模型文件时自动降级到此
// ============================================================
class PseudoEmbeddingProvider : public EmbeddingProvider {
public:
    // dim 默认 768（与 ONNX 模型一致，降级时维度兼容）
    explicit PseudoEmbeddingProvider(int dim = 768) : dim_(dim) {}

    std::vector<float> Encode(const std::string& text) override;
    int GetDim() const override { return dim_; }
    std::string Name() const override { return "pseudo_bow_hash"; }

private:
    int dim_;
};

// ============================================================
// EmbeddingProviderFactory - 根据配置创建对应 Provider
// ============================================================
class EmbeddingProviderFactory {
public:
    // 从 YAML 配置创建 provider
    // 配置示例（config/framework.yaml）：
    //   embedding:
    //     provider: "onnx"           # onnx / pseudo
    //     dim: 768
    //     model_path: "models/bge-base-zh/model.onnx"
    //     tokenizer_path: "models/bge-base-zh/vocab.txt"
    //     max_seq_len: 512
    static std::shared_ptr<EmbeddingProvider> Create(const YAML::Node& config);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_EMBEDDING_PROVIDER_H
