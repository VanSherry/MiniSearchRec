// ============================================================
// MiniSearchRec - EmbeddingProvider Factory + Pseudo 实现
// ============================================================

#include "lib/embedding/embedding_provider.h"
#include "lib/embedding/onnx_embedding_provider.h"
#include "utils/logger.h"

#include <cmath>
#include <cctype>
#include <functional>

namespace minisearchrec {

// ============================================================
// PseudoEmbeddingProvider 实现
// 词袋哈希投影：无任何外部依赖，作为降级方案
// ============================================================

std::vector<float> PseudoEmbeddingProvider::Encode(const std::string& text) {
    // 简单分词：中文按字，英文按词
    std::vector<std::string> terms;
    std::string buf;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = text[i];
        if (c >= 0x80) {
            size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            if (i + len <= text.size()) {
                terms.push_back(text.substr(i, len));
            }
            i += len;
        } else if (std::isalnum(c)) {
            buf += static_cast<char>(std::tolower(c));
            ++i;
        } else {
            if (!buf.empty()) {
                terms.push_back(buf);
                buf.clear();
            }
            ++i;
        }
    }
    if (!buf.empty()) terms.push_back(buf);

    // 哈希投影
    std::vector<float> emb(dim_, 0.0f);
    if (terms.empty()) return emb;

    std::hash<std::string> hasher;
    for (const auto& term : terms) {
        size_t h = hasher(term);
        for (int d = 0; d < dim_; ++d) {
            size_t hd = h ^ (static_cast<size_t>(d) * 2654435761ULL);
            float val = static_cast<float>(static_cast<int32_t>(hd & 0xFFFFFF))
                        / static_cast<float>(0x800000);
            emb[d] += val;
        }
    }

    // L2 归一化
    float norm = 0.0f;
    for (float v : emb) norm += v * v;
    if (norm > 1e-8f) {
        norm = std::sqrt(norm);
        for (float& v : emb) v /= norm;
    }
    return emb;
}

// ============================================================
// EmbeddingProviderFactory 实现
// ============================================================

std::shared_ptr<EmbeddingProvider> EmbeddingProviderFactory::Create(
    const YAML::Node& config) {

    std::string provider_type = "pseudo";
    int dim = 768;

    if (config["provider"]) {
        provider_type = config["provider"].as<std::string>("pseudo");
    }
    if (config["dim"]) {
        dim = config["dim"].as<int>(768);
    }

    // ONNX 模式：内置模型（如 bge-base-zh-v1.5）
    if (provider_type == "onnx") {
        std::string model_path = config["model_path"].as<std::string>("");
        std::string tokenizer_path = config["tokenizer_path"].as<std::string>("");
        int max_seq_len = config["max_seq_len"].as<int>(512);

        if (model_path.empty() || tokenizer_path.empty()) {
            LOG_ERROR("EmbeddingProviderFactory: onnx mode requires model_path and tokenizer_path");
            LOG_WARN("EmbeddingProviderFactory: falling back to pseudo (dim={})", dim);
            return std::make_shared<PseudoEmbeddingProvider>(dim);
        }

        auto provider = std::make_shared<OnnxEmbeddingProvider>();
        if (provider->Init(model_path, tokenizer_path, dim, max_seq_len)) {
            LOG_INFO("EmbeddingProviderFactory: ONNX provider ready, model={}, dim={}",
                     model_path, dim);
            return provider;
        }

        LOG_WARN("EmbeddingProviderFactory: ONNX init failed, falling back to pseudo (dim={})", dim);
        return std::make_shared<PseudoEmbeddingProvider>(dim);
    }

    // 默认：伪 embedding（dim 与 ONNX 保持一致，降级不影响向量索引维度）
    return std::make_shared<PseudoEmbeddingProvider>(dim);
}

} // namespace minisearchrec
