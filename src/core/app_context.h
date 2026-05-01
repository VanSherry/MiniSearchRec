// ============================================================
// MiniSearchRec - 应用全局上下文（依赖注入容器）
// 解决各 Processor 无法获取 InvertedIndex 实例的问题
// 参考：微信搜推的全局服务注册表
// ============================================================

#ifndef MINISEARCHREC_APP_CONTEXT_H
#define MINISEARCHREC_APP_CONTEXT_H

#include <memory>
#include <mutex>
#include "index/inverted_index.h"
#include "index/vector_index.h"
#include "index/doc_store.h"
#include "index/index_builder.h"
#include "ab/ab_test.h"
#include "core/embedding_provider.h"

namespace minisearchrec {

// ============================================================
// AppContext：全局单例，持有所有共享资源
// ============================================================
class AppContext {
public:
    static AppContext& Instance() {
        static AppContext inst;
        return inst;
    }

    // --- 初始化（在 main 中调用一次）---
    bool Initialize(const std::string& data_dir,
                    const std::string& index_dir,
                    bool rebuild_on_start = false);

    // --- 索引访问器 ---
    std::shared_ptr<InvertedIndex> GetInvertedIndex() const {
        return inverted_index_;
    }

    std::shared_ptr<VectorIndex> GetVectorIndex() const {
        return vector_index_;
    }

    std::shared_ptr<DocStore> GetDocStore() const {
        return doc_store_;
    }

    std::shared_ptr<IndexBuilder> GetIndexBuilder() const {
        return index_builder_;
    }

    // --- Embedding 提供器（配置驱动，一键切换）---
    // 保证永远不返回 null：未初始化时使用默认 PseudoEmbeddingProvider
    std::shared_ptr<EmbeddingProvider> GetEmbeddingProvider() const {
        if (!embedding_provider_) {
            static auto default_provider =
                std::make_shared<PseudoEmbeddingProvider>();
            return default_provider;
        }
        return embedding_provider_;
    }

    // --- A/B 实验框架 ---
    std::shared_ptr<ABTestManager> GetABTestManager() const {
        return ab_test_manager_;
    }

    void SetABTestManager(std::shared_ptr<ABTestManager> mgr) {
        ab_test_manager_ = std::move(mgr);
    }

    // --- 是否就绪 ---
    bool IsReady() const { return ready_; }

    // --- 增量添加文档（线程安全，同时持久化索引）---
    bool AddDocument(const Document& doc);

private:
    AppContext() = default;

    std::shared_ptr<InvertedIndex> inverted_index_;
    std::shared_ptr<VectorIndex>   vector_index_;
    std::shared_ptr<DocStore>      doc_store_;
    std::shared_ptr<IndexBuilder>  index_builder_;
    std::shared_ptr<EmbeddingProvider> embedding_provider_;
    std::shared_ptr<ABTestManager> ab_test_manager_;
    bool ready_ = false;
    std::string index_dir_;        // 保存索引目录路径，供 AddDocument 持久化使用
    mutable std::mutex mutex_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_APP_CONTEXT_H
