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

    // --- 是否就绪 ---
    bool IsReady() const { return ready_; }

    // --- 增量添加文档（线程安全）---
    bool AddDocument(const Document& doc);

private:
    AppContext() = default;

    std::shared_ptr<InvertedIndex> inverted_index_;
    std::shared_ptr<VectorIndex>   vector_index_;
    std::shared_ptr<DocStore>      doc_store_;
    std::shared_ptr<IndexBuilder>  index_builder_;
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_APP_CONTEXT_H
