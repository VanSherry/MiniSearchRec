// ============================================================
// MiniSearchRec - ONNX Embedding Provider
// 内置 BERT 类 embedding 模型（如 bge-base-zh-v1.5）
// 服务启动时一次性加载模型到内存，每次请求推理 ~15ms (CPU)
//
// 依赖：onnxruntime C++ SDK
// 模型：ONNX 格式（从 HuggingFace 转换）
// Tokenizer：WordPiece（读取 vocab.txt）
//
// 配置：
//   embedding:
//     provider: "onnx"
//     model_path: "./models/bge-base-zh/model.onnx"
//     tokenizer_path: "./models/bge-base-zh/vocab.txt"
//     dim: 768
//     max_seq_len: 512
// ============================================================

#pragma once

#include "lib/embedding/embedding_provider.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

// 条件编译：只有链接了 onnxruntime 才启用
#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace minisearchrec {

// ============================================================
// WordPiece Tokenizer（简化实现）
// 读取 BERT vocab.txt，实现 WordPiece 分词
// ============================================================
class WordPieceTokenizer {
public:
    bool Load(const std::string& vocab_path);

    // Tokenize + 转 ID（含 [CLS] + [SEP]）
    struct TokenResult {
        std::vector<int64_t> input_ids;
        std::vector<int64_t> attention_mask;
        std::vector<int64_t> token_type_ids;
    };

    TokenResult Encode(const std::string& text, int max_seq_len = 512) const;

    bool IsLoaded() const { return !vocab_.empty(); }
    int VocabSize() const { return static_cast<int>(vocab_.size()); }

private:
    // vocab.txt 中 token → id
    std::unordered_map<std::string, int> vocab_;
    int cls_id_ = 101;   // [CLS]
    int sep_id_ = 102;   // [SEP]
    int unk_id_ = 100;   // [UNK]
    int pad_id_ = 0;     // [PAD]

    // 基础 tokenize：中文按字，英文按词，再 WordPiece 切分
    std::vector<std::string> BasicTokenize(const std::string& text) const;
    std::vector<std::string> WordPieceTokenize(const std::string& token) const;
    int TokenToId(const std::string& token) const;
};

// ============================================================
// OnnxEmbeddingProvider
// ============================================================
class OnnxEmbeddingProvider : public EmbeddingProvider {
public:
    OnnxEmbeddingProvider() = default;
    ~OnnxEmbeddingProvider() override = default;

    // 初始化：加载 ONNX 模型 + vocab
    bool Init(const std::string& model_path,
              const std::string& tokenizer_path,
              int dim = 768,
              int max_seq_len = 512);

    // 单条编码
    std::vector<float> Encode(const std::string& text) override;

    // 批量编码（一次推理多条，更高效）
    std::vector<std::vector<float>> EncodeBatch(
        const std::vector<std::string>& texts) override;

    int GetDim() const override { return dim_; }
    std::string Name() const override { return "onnx_bge_base_zh"; }
    bool IsReady() const { return ready_; }

private:
    // Mean pooling: 对 token embeddings 按 attention_mask 加权平均
    std::vector<float> MeanPooling(const float* token_embeddings,
                                   const std::vector<int64_t>& attention_mask,
                                   int seq_len, int hidden_dim) const;

    // L2 归一化
    void L2Normalize(std::vector<float>& vec) const;

    WordPieceTokenizer tokenizer_;
    int dim_ = 768;
    int max_seq_len_ = 512;
    bool ready_ = false;

#ifdef HAVE_ONNXRUNTIME
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "MiniSearchRec"};
    std::unique_ptr<Ort::Session> session_;
    Ort::SessionOptions session_options_;
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);
#endif

    mutable std::mutex mutex_;  // 推理线程安全
};

} // namespace minisearchrec
