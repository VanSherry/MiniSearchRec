// ============================================================
// MiniSearchRec - 查询解析器
// 参考：微信 QP 模块、X(Twitter) QueryParser
// 作用：将原始查询解析为结构化 QPInfo
// ============================================================

#include "query/query_parser.h"
#include "query/query_normalizer.h"
#include "query/query_expander.h"
#include "utils/string_utils.h"
#include "core/app_context.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cmath>
#include <functional>

namespace minisearchrec {

// ============================================================
// 生成轻量伪 query embedding（词袋 BoW 哈希投影）
// 用途：在没有外部 embedding 服务时，让 VectorRecall 流程可以正常运行。
// 实现：把每个 term 的 hash 值散射到 kEmbDim 维归一化向量。
// 注意：若部署了真实 embedding 服务，在此处替换调用即可。
// ============================================================
static constexpr int kEmbDim = 64;  // 与 VectorIndex 构建时的维度保持一致

static std::vector<float> BuildPseudoEmbedding(
    const std::vector<std::string>& terms)
{
    std::vector<float> emb(kEmbDim, 0.0f);
    if (terms.empty()) return emb;

    std::hash<std::string> hasher;
    for (const auto& term : terms) {
        size_t h = hasher(term);
        // 将 hash 值散射到多个维度
        for (int d = 0; d < kEmbDim; ++d) {
            // 不同维度用不同的 hash 混合
            size_t hd = h ^ (static_cast<size_t>(d) * 2654435761ULL);
            // 映射到 [-1, 1]
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

void QueryParser::Parse(const std::string& raw_query, QPInfo& qp_info) const {
    // 1. 设置原始查询
    qp_info.raw_query = raw_query;
    
    // 2. 归一化
    QueryNormalizer normalizer;
    std::string normalized = normalizer.Normalize(raw_query);
    qp_info.normalized_query = normalized;
    
    // 3. 分词
    qp_info.terms = Tokenize(normalized);
    
    // 4. 查询扩展
    QueryExpander expander;
    qp_info.terms = expander.Expand(qp_info.terms);
    
    // 5. 推断类别
    qp_info.inferred_category = InferCategory(qp_info.terms);
    
    // 6. 计算 IDF（需要倒排索引，这里先初始化为 1.0）
    for (const auto& term : qp_info.terms) {
        qp_info.term_idf[term] = 1.0f;  // 实际项目需从索引获取
    }

    // 7. 生成 query embedding（词袋伪向量，保证 VectorRecall 流程可运行）
    //    若已有外部服务填充了 query_embedding，跳过此步。
    if (qp_info.query_embedding.empty() && !qp_info.terms.empty()) {
        qp_info.query_embedding = BuildPseudoEmbedding(qp_info.terms);
    }
}

std::vector<std::string> QueryParser::Tokenize(const std::string& text) const {
    return utils::Tokenize(text);
}

std::string QueryParser::InferCategory(const std::vector<std::string>& terms) const {
    // 简化实现：基于关键词匹配
    static const std::unordered_map<std::string, std::string> keyword_category = {
        {"手机", "technology"},
        {"电脑", "technology"},
        {"AI", "technology"},
        {"美食", "food"},
        {"餐厅", "food"},
        {"旅游", "travel"},
        {"酒店", "travel"},
    };
    
    for (const auto& term : terms) {
        auto it = keyword_category.find(term);
        if (it != keyword_category.end()) {
            return it->second;
        }
    }
    
    return "general";
}

void QueryParser::SetStopWords(const std::vector<std::string>& stop_words) {
    stop_words_ = stop_words;
}

void QueryParser::SetSynonymDict(
    const std::unordered_map<std::string, std::vector<std::string>>& synonym_dict) {
    synonym_dict_ = synonym_dict;
}

bool QueryParser::ValidateQuery(const std::string& raw_query,
                                std::string& error_msg) const {
    if (raw_query.empty()) {
        error_msg = "Query is empty";
        return false;
    }
    
    if (raw_query.length() > 200) {
        error_msg = "Query too long (max 200 characters)";
        return false;
    }
    
    // 检查是否包含非法字符（简化）
    for (char c : raw_query) {
        if (c == '\0' || c == '\n' || c == '\r') {
            error_msg = "Query contains invalid characters";
            return false;
        }
    }
    
    return true;
}

} // namespace minisearchrec
