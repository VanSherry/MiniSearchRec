// ============================================================
// MiniSearchRec - Embedding 提供器抽象层
// 通过配置一键切换伪 embedding / 真实 embedding 服务
// ============================================================

#ifndef MINISEARCHREC_EMBEDDING_PROVIDER_H
#define MINISEARCHREC_EMBEDDING_PROVIDER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
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

    // 批量编码（默认逐条调用，子类可覆盖为批量请求）
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
// PseudoEmbeddingProvider - 词袋哈希投影（默认内置）
// 无需外部依赖，让 VectorRecall 流程可运行
// ============================================================
class PseudoEmbeddingProvider : public EmbeddingProvider {
public:
    explicit PseudoEmbeddingProvider(int dim = 64) : dim_(dim) {}

    std::vector<float> Encode(const std::string& text) override {
        auto terms = SimpleTokenize(text);
        return BuildFromTerms(terms);
    }

    int GetDim() const override { return dim_; }
    std::string Name() const override { return "pseudo_bow_hash"; }

private:
    int dim_;

    // 简单分词：中文按字，英文按词
    std::vector<std::string> SimpleTokenize(const std::string& text) const {
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
        return terms;
    }

    std::vector<float> BuildFromTerms(const std::vector<std::string>& terms) const {
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
};

// ============================================================
// HttpEmbeddingProvider - 调用远程 HTTP embedding 服务
// 支持 OpenAI / BGE / 自建服务等兼容接口
// ============================================================
class HttpEmbeddingProvider : public EmbeddingProvider {
public:
    HttpEmbeddingProvider(const std::string& endpoint,
                          int dim,
                          const std::string& model_name = "",
                          const std::string& api_key = "")
        : endpoint_(endpoint), dim_(dim),
          model_name_(model_name), api_key_(api_key) {}

    std::vector<float> Encode(const std::string& text) override {
        // TODO: 实现 HTTP POST 调用
        // POST endpoint_ { "input": text, "model": model_name_ }
        // 返回 embedding 向量
        // 当前 fallback 到伪 embedding（避免编译期依赖 curl）
        PseudoEmbeddingProvider fallback(dim_);
        return fallback.Encode(text);
    }

    int GetDim() const override { return dim_; }
    std::string Name() const override { return "http_" + model_name_; }

private:
    std::string endpoint_;
    int dim_;
    std::string model_name_;
    std::string api_key_;
};

// ============================================================
// EmbeddingProviderFactory - 根据配置创建对应 Provider
// ============================================================
class EmbeddingProviderFactory {
public:
    // 从 YAML 配置创建 provider
    // 配置示例：
    //   embedding:
    //     provider: "pseudo"        # pseudo / http
    //     dim: 64
    //     # 以下仅 http 模式需要：
    //     endpoint: "http://localhost:8000/v1/embeddings"
    //     model: "bge-base-zh-v1.5"
    //     api_key: "sk-xxx"
    static std::shared_ptr<EmbeddingProvider> Create(const YAML::Node& config) {
        std::string provider_type = "pseudo";
        int dim = 64;

        if (config["provider"]) {
            provider_type = config["provider"].as<std::string>("pseudo");
        }
        if (config["dim"]) {
            dim = config["dim"].as<int>(64);
        }

        if (provider_type == "http") {
            std::string endpoint = config["endpoint"].as<std::string>("");
            std::string model = config["model"].as<std::string>("");
            std::string api_key = config["api_key"].as<std::string>("");
            return std::make_shared<HttpEmbeddingProvider>(
                endpoint, dim, model, api_key);
        }

        // 默认：伪 embedding
        return std::make_shared<PseudoEmbeddingProvider>(dim);
    }
};

} // namespace minisearchrec

#endif // MINISEARCHREC_EMBEDDING_PROVIDER_H
