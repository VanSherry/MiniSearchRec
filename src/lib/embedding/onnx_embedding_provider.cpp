// ============================================================
// MiniSearchRec - ONNX Embedding Provider 实现
// ============================================================

#include "lib/embedding/onnx_embedding_provider.h"
#include "utils/logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <cctype>

namespace minisearchrec {

// ============================================================
// WordPieceTokenizer 实现
// ============================================================

bool WordPieceTokenizer::Load(const std::string& vocab_path) {
    std::ifstream f(vocab_path);
    if (!f.is_open()) {
        LOG_ERROR("WordPieceTokenizer: failed to open vocab: {}", vocab_path);
        return false;
    }

    std::string line;
    int id = 0;
    while (std::getline(f, line)) {
        // 去掉行尾 \r（Windows 格式兼容）
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vocab_[line] = id++;
    }

    // 查找特殊 token ID
    auto find_id = [&](const std::string& tok, int default_id) {
        auto it = vocab_.find(tok);
        return (it != vocab_.end()) ? it->second : default_id;
    };

    cls_id_ = find_id("[CLS]", 101);
    sep_id_ = find_id("[SEP]", 102);
    unk_id_ = find_id("[UNK]", 100);
    pad_id_ = find_id("[PAD]", 0);

    LOG_INFO("WordPieceTokenizer: loaded {} tokens from {}", vocab_.size(), vocab_path);
    return !vocab_.empty();
}

std::vector<std::string> WordPieceTokenizer::BasicTokenize(const std::string& text) const {
    // 1. 小写化
    // 2. 中文字符前后加空格（按字切分）
    // 3. 按空格分词
    std::string processed;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = text[i];
        if (c >= 0xE0 && i + 2 < text.size()) {
            // UTF-8 3字节（中文）→ 前后加空格
            processed += ' ';
            processed += text.substr(i, 3);
            processed += ' ';
            i += 3;
        } else if (c >= 0xC0 && i + 1 < text.size()) {
            processed += text.substr(i, 2);
            i += 2;
        } else if (c >= 0xF0 && i + 3 < text.size()) {
            processed += ' ';
            processed += text.substr(i, 4);
            processed += ' ';
            i += 4;
        } else {
            processed += static_cast<char>(std::tolower(c));
            ++i;
        }
    }

    // 按空格/标点分词
    std::vector<std::string> tokens;
    std::string buf;
    for (char c : processed) {
        if (std::isspace(c) || std::ispunct(c)) {
            if (!buf.empty()) { tokens.push_back(buf); buf.clear(); }
            if (std::ispunct(c)) tokens.push_back(std::string(1, c));
        } else {
            buf += c;
        }
    }
    if (!buf.empty()) tokens.push_back(buf);
    return tokens;
}

std::vector<std::string> WordPieceTokenizer::WordPieceTokenize(const std::string& token) const {
    std::vector<std::string> sub_tokens;
    if (token.empty()) return sub_tokens;

    // 如果整个 token 在词表中，直接返回
    if (vocab_.count(token)) {
        sub_tokens.push_back(token);
        return sub_tokens;
    }

    // WordPiece 贪心分割
    size_t start = 0;
    while (start < token.size()) {
        size_t end = token.size();
        std::string cur_sub;
        bool found = false;

        while (start < end) {
            std::string sub = token.substr(start, end - start);
            if (start > 0) sub = "##" + sub;

            if (vocab_.count(sub)) {
                cur_sub = sub;
                found = true;
                break;
            }
            // 缩短（按 UTF-8 字符边界）
            --end;
            while (end > start && (token[end] & 0xC0) == 0x80) --end;
        }

        if (!found) {
            sub_tokens.push_back("[UNK]");
            break;
        }

        sub_tokens.push_back(cur_sub);
        start = (start > 0) ? end : end;
        if (start == 0) start = end;  // 避免死循环
    }

    return sub_tokens;
}

int WordPieceTokenizer::TokenToId(const std::string& token) const {
    auto it = vocab_.find(token);
    return (it != vocab_.end()) ? it->second : unk_id_;
}

WordPieceTokenizer::TokenResult WordPieceTokenizer::Encode(
    const std::string& text, int max_seq_len) const {
    TokenResult result;

    auto basic_tokens = BasicTokenize(text);

    // WordPiece 切分
    std::vector<int64_t> token_ids;
    token_ids.push_back(cls_id_);  // [CLS]

    for (const auto& token : basic_tokens) {
        auto sub_tokens = WordPieceTokenize(token);
        for (const auto& sub : sub_tokens) {
            token_ids.push_back(TokenToId(sub));
            if (static_cast<int>(token_ids.size()) >= max_seq_len - 1) break;
        }
        if (static_cast<int>(token_ids.size()) >= max_seq_len - 1) break;
    }

    token_ids.push_back(sep_id_);  // [SEP]

    int seq_len = static_cast<int>(token_ids.size());

    // Padding
    result.input_ids = token_ids;
    result.attention_mask.assign(seq_len, 1);
    result.token_type_ids.assign(seq_len, 0);

    // Pad to max_seq_len（ONNX 需要固定长度，或用动态 shape）
    // 这里使用动态长度（不 pad），ONNX 模型需支持动态 batch/seq
    return result;
}

// ============================================================
// OnnxEmbeddingProvider 实现
// ============================================================

bool OnnxEmbeddingProvider::Init(const std::string& model_path,
                                  const std::string& tokenizer_path,
                                  int dim, int max_seq_len) {
    dim_ = dim;
    max_seq_len_ = max_seq_len;

    // 1. 加载 tokenizer
    if (!tokenizer_.Load(tokenizer_path)) {
        LOG_ERROR("OnnxEmbeddingProvider: failed to load tokenizer from {}", tokenizer_path);
        return false;
    }

#ifdef HAVE_ONNXRUNTIME
    // 2. 加载 ONNX 模型
    try {
        session_options_.SetIntraOpNumThreads(2);  // 推理线程数
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(
            env_, model_path.c_str(), session_options_);

        LOG_INFO("OnnxEmbeddingProvider: model loaded from {}", model_path);

        // 验证输入输出
        auto input_count = session_->GetInputCount();
        auto output_count = session_->GetOutputCount();
        LOG_INFO("OnnxEmbeddingProvider: inputs={}, outputs={}", input_count, output_count);

        for (size_t i = 0; i < input_count; ++i) {
            auto name = session_->GetInputNameAllocated(i, Ort::AllocatorWithDefaultOptions());
            LOG_INFO("  input[{}]: {}", i, name.get());
        }
        for (size_t i = 0; i < output_count; ++i) {
            auto name = session_->GetOutputNameAllocated(i, Ort::AllocatorWithDefaultOptions());
            LOG_INFO("  output[{}]: {}", i, name.get());
        }

        ready_ = true;
        return true;

    } catch (const Ort::Exception& e) {
        LOG_ERROR("OnnxEmbeddingProvider: failed to load model: {}", e.what());
        return false;
    }
#else
    LOG_WARN("OnnxEmbeddingProvider: ONNX Runtime not available (HAVE_ONNXRUNTIME not defined)");
    LOG_WARN("OnnxEmbeddingProvider: falling back to tokenizer-only mode (pseudo embedding)");
    // 无 ONNX Runtime 时降级：用 tokenizer 的 token ID 做哈希投影（比纯 PseudoEmbedding 好一点）
    ready_ = true;
    return true;
#endif
}

std::vector<float> OnnxEmbeddingProvider::Encode(const std::string& text) {
    if (!ready_) {
        return std::vector<float>(dim_, 0.0f);
    }

    auto tokens = tokenizer_.Encode(text, max_seq_len_);

#ifdef HAVE_ONNXRUNTIME
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        int64_t seq_len = static_cast<int64_t>(tokens.input_ids.size());
        std::array<int64_t, 2> input_shape = {1, seq_len};

        // 创建输入 tensors
        auto input_ids_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info_, tokens.input_ids.data(), tokens.input_ids.size(),
            input_shape.data(), input_shape.size());

        auto attention_mask_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info_, tokens.attention_mask.data(), tokens.attention_mask.size(),
            input_shape.data(), input_shape.size());

        auto token_type_ids_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info_, tokens.token_type_ids.data(), tokens.token_type_ids.size(),
            input_shape.data(), input_shape.size());

        // 输入名称（BERT 标准）
        const char* input_names[] = {"input_ids", "attention_mask", "token_type_ids"};
        const char* output_names[] = {"last_hidden_state"};

        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(input_ids_tensor));
        inputs.push_back(std::move(attention_mask_tensor));
        inputs.push_back(std::move(token_type_ids_tensor));

        // 推理
        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names, inputs.data(), inputs.size(),
            output_names, 1);

        // 输出: [1, seq_len, hidden_dim]
        auto& output_tensor = outputs[0];
        auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
        int hidden_dim = static_cast<int>(output_shape.back());

        const float* output_data = output_tensor.GetTensorData<float>();

        // Mean pooling
        auto embedding = MeanPooling(output_data, tokens.attention_mask,
                                     static_cast<int>(seq_len), hidden_dim);
        L2Normalize(embedding);
        return embedding;

    } catch (const Ort::Exception& e) {
        LOG_ERROR("OnnxEmbeddingProvider::Encode: inference failed: {}", e.what());
        return std::vector<float>(dim_, 0.0f);
    }
#else
    // 无 ONNX Runtime：用 token ID 做哈希投影（比纯文本哈希更好）
    std::vector<float> emb(dim_, 0.0f);
    std::hash<int64_t> hasher;
    for (auto id : tokens.input_ids) {
        size_t h = hasher(id);
        for (int d = 0; d < dim_; ++d) {
            size_t hd = h ^ (static_cast<size_t>(d) * 2654435761ULL);
            float val = static_cast<float>(static_cast<int32_t>(hd & 0xFFFFFF))
                        / static_cast<float>(0x800000);
            emb[d] += val;
        }
    }
    L2Normalize(emb);
    return emb;
#endif
}

std::vector<std::vector<float>> OnnxEmbeddingProvider::EncodeBatch(
    const std::vector<std::string>& texts) {
    // 逐条推理（简单实现，可后续优化为 batch inference）
    std::vector<std::vector<float>> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        results.push_back(Encode(text));
    }
    return results;
}

std::vector<float> OnnxEmbeddingProvider::MeanPooling(
    const float* token_embeddings,
    const std::vector<int64_t>& attention_mask,
    int seq_len, int hidden_dim) const {

    std::vector<float> result(hidden_dim, 0.0f);
    float total_weight = 0.0f;

    for (int i = 0; i < seq_len; ++i) {
        if (attention_mask[i] == 0) continue;
        float w = static_cast<float>(attention_mask[i]);
        total_weight += w;
        for (int d = 0; d < hidden_dim; ++d) {
            result[d] += token_embeddings[i * hidden_dim + d] * w;
        }
    }

    if (total_weight > 0.0f) {
        for (float& v : result) v /= total_weight;
    }

    return result;
}

void OnnxEmbeddingProvider::L2Normalize(std::vector<float>& vec) const {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    if (norm > 1e-8f) {
        norm = std::sqrt(norm);
        for (float& v : vec) v /= norm;
    }
}

} // namespace minisearchrec
