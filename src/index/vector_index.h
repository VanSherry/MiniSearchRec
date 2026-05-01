// ===========================================================
// MiniSearchRec - 向量索引（Faiss 封装）
// 参考：X SimClusters ANN、微信 HNSWSQ
// ===========================================================

#ifndef MINISEARCHREC_VECTOR_INDEX_H
#define MINISEARCHREC_VECTOR_INDEX_H

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

// Faiss 条件编译支持
#ifdef HAVE_FAISS
#include <faiss/IndexHNSW.h>
#include <faiss/IndexFlat.h>
#endif

namespace minisearchrec {

// ===========================================================
// 向量索引配置
// ===========================================================
struct VectorIndexConfig {
    int dim = 768;              // 向量维度（BERT: 768, MiniLM: 384）
    int m = 32;                 // HNSW 参数：每层邻居数
    int ef_construction = 200;   // 建库时的搜索宽度
    int ef_search = 64;         // 查询时的搜索宽度
};

// ===========================================================
// 向量索引封装（基于 Faiss HNSW）
// ===========================================================
class VectorIndex {
public:
    explicit VectorIndex(const VectorIndexConfig& config);
    ~VectorIndex();

    // 检查是否启用了 Faiss
    bool HasFaiss() const {
#ifdef HAVE_FAISS
        return hnsw_index_ != nullptr;
#else
        return false;
#endif
    }

    // 添加单个文档向量
    bool AddVector(const std::string& doc_id,
                   const std::vector<float>& embedding);

    // 批量添加文档向量
    bool AddVectors(const std::vector<std::string>& doc_ids,
                    const std::vector<std::vector<float>>& embeddings);

    // 向量近似最近邻搜索
    std::vector<std::pair<std::string, float>> Search(
        const std::vector<float>& query_embedding,
        int top_k = 200,
        float similarity_threshold = 0.7f
    );

    // 持久化
    bool Save(const std::string& path);
    bool Load(const std::string& path);

    // 获取索引信息
    size_t GetVectorCount() const;
    int GetDim() const { return config_.dim; }

private:
    VectorIndexConfig config_;

#ifdef HAVE_FAISS
    // Faiss HNSW 索引
    faiss::IndexHNSWFlat* hnsw_index_ = nullptr;
    // 维护 doc_id 到向量下标的映射
    std::vector<std::string> doc_ids_;
#else
    // Fallback: 暴力搜索实现
    std::unordered_map<std::string, std::vector<float>> vectors_;
    std::vector<std::string> id_map_;
#endif

    // 读写锁
    mutable std::shared_mutex rwlock_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_VECTOR_INDEX_H
