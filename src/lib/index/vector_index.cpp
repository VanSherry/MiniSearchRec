// ==========================================================
// MiniSearchRec - 向量索引实现（Faiss 封装）
// ==========================================================

#include "lib/index/vector_index.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <algorithm>

#ifdef HAVE_FAISS
#include <faiss/index_io.h>
#endif

namespace minisearchrec {

namespace {

// 计算余弦相似度（用于 Fallback 暴力搜索）
float CosineSimilarity(const std::vector<float>& a,
                      const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float norm = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (norm < 1e-6f) return 0.0f;
    return dot / norm;
}

} // anonymous namespace

VectorIndex::VectorIndex(const VectorIndexConfig& config)
    : config_(config) {
#ifdef HAVE_FAISS
    hnsw_index_ = new faiss::IndexHNSWFlat(config_.dim, config_.m);
    hnsw_index_->hnsw.efConstruction = config_.ef_construction;
    doc_ids_.reserve(10000);
    std::cout << "[VectorIndex] Using Faiss HNSW (dim=" << config_.dim
              << ", m=" << config_.m << ")\n";
#else
    vectors_.reserve(10000);
    id_map_.reserve(10000);
    std::cout << "[VectorIndex] Using Fallback Brute-force (dim=" << config_.dim << ")\n";
#endif
}

VectorIndex::~VectorIndex() {
#ifdef HAVE_FAISS
    if (hnsw_index_) {
        delete hnsw_index_;
        hnsw_index_ = nullptr;
    }
#endif
}

size_t VectorIndex::GetVectorCount() const {
#ifdef HAVE_FAISS
    return doc_ids_.size();
#else
    return id_map_.size();
#endif
}

bool VectorIndex::AddVector(const std::string& doc_id,
                           const std::vector<float>& embedding) {
    std::unique_lock<std::shared_mutex> lock(rwlock_);

    if (embedding.size() != static_cast<size_t>(config_.dim)) {
        std::cerr << "[VectorIndex] Embedding dim mismatch: "
                  << embedding.size() << " vs " << config_.dim << "\n";
        return false;
    }

#ifdef HAVE_FAISS
    if (hnsw_index_) {
        hnsw_index_->add(1, embedding.data());
        doc_ids_.push_back(doc_id);
    }
#else
    vectors_[doc_id] = embedding;
    id_map_.push_back(doc_id);
#endif

    return true;
}

bool VectorIndex::AddVectors(const std::vector<std::string>& doc_ids,
                            const std::vector<std::vector<float>>& embeddings) {
    if (doc_ids.size() != embeddings.size()) return false;

#ifdef HAVE_FAISS
    std::unique_lock<std::shared_mutex> lock(rwlock_);

    if (hnsw_index_ && !embeddings.empty()) {
        size_t count = embeddings.size();

        // 验证所有向量维度
        for (size_t i = 0; i < count; ++i) {
            if (embeddings[i].size() != static_cast<size_t>(config_.dim)) {
                std::cerr << "[VectorIndex] Embedding dim mismatch at index "
                          << i << "\n";
                return false;
            }
        }

        // 构建连续内存用于批量添加
        std::vector<float> all_embeddings;
        all_embeddings.reserve(count * config_.dim);
        for (const auto& emb : embeddings) {
            all_embeddings.insert(all_embeddings.end(), emb.begin(), emb.end());
        }

        hnsw_index_->add(count, all_embeddings.data());

        for (const auto& id : doc_ids) {
            doc_ids_.push_back(id);
        }
    }
#else
    for (size_t i = 0; i < doc_ids.size(); ++i) {
        AddVector(doc_ids[i], embeddings[i]);
    }
#endif

    return true;
}

std::vector<std::pair<std::string, float>> VectorIndex::Search(
    const std::vector<float>& query_embedding,
    int top_k,
    float similarity_threshold
) {
    std::shared_lock<std::shared_mutex> lock(rwlock_);

    std::vector<std::pair<std::string, float>> results;

#ifdef HAVE_FAISS
    if (hnsw_index_ && hnsw_index_->ntotal > 0) {
        hnsw_index_->hnsw.efSearch = config_.ef_search;

        std::vector<float> distances(top_k);
        std::vector<faiss::idx_t> labels(top_k);

        hnsw_index_->search(1, query_embedding.data(), top_k,
                           distances.data(), labels.data());

        for (int i = 0; i < top_k; ++i) {
            if (labels[i] == -1) break;

            float l2_dist = std::sqrt(distances[i]);
            float similarity = 1.0f - l2_dist * l2_dist / 2.0f;

            if (similarity >= similarity_threshold) {
                int idx = static_cast<int>(labels[i]);
                if (idx >= 0 && idx < (int)doc_ids_.size()) {
                    results.emplace_back(doc_ids_[idx], similarity);
                }
            }
        }
    }
#else
    for (const auto& [doc_id, embedding] : vectors_) {
        float sim = CosineSimilarity(query_embedding, embedding);
        if (sim >= similarity_threshold) {
            results.emplace_back(doc_id, sim);
        }
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
#endif

    if ((int)results.size() > top_k) {
        results.resize(top_k);
    }

    return results;
}

bool VectorIndex::Save(const std::string& path) {
    std::unique_lock<std::shared_mutex> lock(rwlock_);

#ifdef HAVE_FAISS
    if (hnsw_index_) {
        try {
            faiss::write_index(hnsw_index_, path.c_str());
            std::cout << "[VectorIndex] Saved Faiss index to " << path << "\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[VectorIndex] Failed to save Faiss index: "
                      << e.what() << "\n";
            return false;
        }
    }
    return false;
#else
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    int32_t dim = config_.dim;
    ofs.write(reinterpret_cast<const char*>(&dim), sizeof(dim));

    int32_t count = static_cast<int32_t>(vectors_.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [doc_id, embedding] : vectors_) {
        int32_t id_len = static_cast<int32_t>(doc_id.size());
        ofs.write(reinterpret_cast<const char*>(&id_len), sizeof(id_len));
        ofs.write(doc_id.data(), id_len);
        ofs.write(reinterpret_cast<const char*>(embedding.data()),
                   embedding.size() * sizeof(float));
    }

    std::cout << "[VectorIndex] Saved " << count << " vectors to " << path << "\n";
    return true;
#endif
}

bool VectorIndex::Load(const std::string& path) {
    std::unique_lock<std::shared_mutex> lock(rwlock_);

#ifdef HAVE_FAISS
    try {
        faiss::Index* idx = faiss::read_index(path.c_str());
        if (!idx) {
            std::cerr << "[VectorIndex] Failed to load Faiss index from "
                      << path << "\n";
            return false;
        }

        if (idx->d != config_.dim) {
            std::cerr << "[VectorIndex] Dimension mismatch: loaded="
                      << idx->d << ", config=" << config_.dim << "\n";
            delete idx;
            return false;
        }

        if (hnsw_index_) {
            delete hnsw_index_;
        }
        hnsw_index_ = dynamic_cast<faiss::IndexHNSWFlat*>(idx);
        if (!hnsw_index_) {
            std::cerr << "[VectorIndex] Loaded index is not HNSWFlat\n";
            delete idx;
            return false;
        }

        // 注意：doc_ids_ 需要单独保存/加载
        std::cerr << "[VectorIndex] Warning: doc_ids not loaded from Faiss index file\n";
        std::cerr << "[VectorIndex] You may need to rebuild doc_ids mapping\n";

        std::cout << "[VectorIndex] Loaded Faiss index from " << path
                  << " (ntotal=" << hnsw_index_->ntotal << ")\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VectorIndex] Exception loading Faiss index: "
                  << e.what() << "\n";
        return false;
    }
#else
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;

    int32_t dim;
    ifs.read(reinterpret_cast<char*>(&dim), sizeof(dim));
    if (dim != config_.dim) {
        std::cerr << "[VectorIndex] Dim mismatch in loaded file\n";
        return false;
    }

    int32_t count;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

    for (int32_t i = 0; i < count; ++i) {
        int32_t id_len;
        ifs.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));
        std::string doc_id(id_len, ' ');
        ifs.read(&doc_id[0], id_len);

        std::vector<float> embedding(dim);
        ifs.read(reinterpret_cast<char*>(embedding.data()),
                  dim * sizeof(float));

        vectors_[doc_id] = embedding;
        id_map_.push_back(doc_id);
    }

    std::cout << "[VectorIndex] Loaded " << count << " vectors from "
              << path << "\n";
    return true;
#endif
}

} // namespace minisearchrec
