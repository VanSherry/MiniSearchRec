// ============================================================
// MiniSearchRec - AppContext 实现
// ============================================================

#include "framework/app_context.h"
#include "framework/config/config_manager.h"
#include "utils/logger.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace minisearchrec {

bool AppContext::Initialize(const std::string& data_dir,
                             const std::string& index_dir,
                             bool rebuild_on_start) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    index_dir_ = index_dir;  // 保存供 AddDocument 使用

    LOG_INFO("AppContext initializing, data_dir={}, index_dir={}, rebuild={}",
             data_dir, index_dir, rebuild_on_start);

    // 0. 创建 Embedding 提供器（从配置驱动）
    auto& cfg_mgr = ConfigManager::Instance();
    YAML::Node emb_cfg;
    if (cfg_mgr.IsLoaded()) {
        const auto& emb = cfg_mgr.GetGlobalConfig().embedding;
        emb_cfg["provider"] = emb.provider;
        emb_cfg["dim"] = emb.dim;
        emb_cfg["model_path"] = emb.model_path;
        emb_cfg["tokenizer_path"] = emb.tokenizer_path;
        emb_cfg["max_seq_len"] = emb.max_seq_len;
    }
    embedding_provider_ = EmbeddingProviderFactory::Create(emb_cfg);
    LOG_INFO("EmbeddingProvider: type={}, dim={}",
             embedding_provider_->Name(), embedding_provider_->GetDim());

    // 1. 创建倒排索引实例
    inverted_index_ = std::make_shared<InvertedIndex>();

    // 2. 创建向量索引实例（维度从 EmbeddingProvider 获取，保证一致）
    VectorIndexConfig vec_cfg;
    vec_cfg.dim = embedding_provider_->GetDim();
    vector_index_ = std::make_shared<VectorIndex>(vec_cfg);

    // 3. 创建文档存储（SQLite）
    doc_store_ = std::make_shared<DocStore>();
    std::string db_path = index_dir + "/docs.db";

    // 确保 index_dir 存在
    try {
        std::filesystem::create_directories(index_dir);
    } catch (...) {
        // 如果 filesystem 不支持，继续
    }

    if (!doc_store_->Open(db_path)) {
        LOG_WARN("DocStore open failed, path={}, continuing without persistence", db_path);
        // 不阻塞启动，继续使用内存模式
    }

    // 4. 创建索引构建器
    index_builder_ = std::make_shared<IndexBuilder>();
    index_builder_->SetInvertedIndex(inverted_index_);
    index_builder_->SetVectorIndex(vector_index_);
    index_builder_->SetDocStore(doc_store_);

    std::string inv_path = index_dir + "/inverted.idx";
    bool index_loaded = false;

    if (!rebuild_on_start) {
        // 尝试从磁盘加载已有索引
        if (index_builder_->LoadIndexes(index_dir)) {
            LOG_INFO("Indexes loaded from disk, doc_count={}",
                     inverted_index_->GetDocCount());
            index_loaded = true;
        } else {
            LOG_INFO("No existing indexes found, will build from data");
        }
    }

    if (!index_loaded) {
        // 优先从 SQLite docs.db 重建索引（包含所有通过 doc/add 动态添加的文章）
        bool rebuilt_from_db = false;
        if (doc_store_) {
            auto all_ids = doc_store_->GetAllDocIds();
            if (!all_ids.empty()) {
                LOG_INFO("Rebuilding index from SQLite docs.db, doc_count={}",
                         all_ids.size());
                std::vector<Document> docs;
                for (const auto& id : all_ids) {
                    Document doc;
                    if (doc_store_->GetDoc(id, doc)) {
                        docs.push_back(doc);
                    }
                }
                if (index_builder_->BuildFromDocs(docs)) {
                    LOG_INFO("Index rebuilt from SQLite, doc_count={}",
                             inverted_index_->GetDocCount());
                    index_builder_->SaveIndexes(index_dir);
                    rebuilt_from_db = true;
                }
            }
        }

        if (!rebuilt_from_db) {
            // SQLite 为空时回退到 sample_docs.json
            std::string json_path = data_dir + "/sample_docs.json";
            std::ifstream f(json_path);
            if (f.good()) {
                f.close();
                LOG_INFO("Building index from: {}", json_path);
                if (!index_builder_->BuildFromJson(json_path)) {
                    LOG_WARN("Failed to build index from {}", json_path);
                } else {
                    LOG_INFO("Index built successfully, doc_count={}",
                             inverted_index_->GetDocCount());
                    index_builder_->SaveIndexes(index_dir);
                }
            } else {
                LOG_WARN("Data file not found: {}, starting with empty index", json_path);
            }
        }
    }

    ready_ = true;
    LOG_INFO("AppContext initialized, inverted_index doc_count={}",
             inverted_index_->GetDocCount());
    return true;
}

bool AppContext::AddDocument(const Document& doc) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);  // BUG-4 修复：加锁保护
    if (!index_builder_) return false;
    bool ok = index_builder_->AddDocument(doc);
    // 同步刷盘（加锁保证安全，后台调度器会定时触发全量重建）
    if (ok && !index_dir_.empty()) {
        index_builder_->SaveIndexes(index_dir_);
    }
    return ok;
}

} // namespace minisearchrec
