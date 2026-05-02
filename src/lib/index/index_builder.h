// ============================================================
// MiniSearchRec - 索引构建器
// 负责从数据源构建倒排索引和向量索引
// ============================================================

#ifndef MINISEARCHREC_INDEX_BUILDER_H
#define MINISEARCHREC_INDEX_BUILDER_H

#include <string>
#include <vector>
#include "lib/index/inverted_index.h"
#include "lib/index/vector_index.h"
#include "lib/index/doc_store.h"

namespace minisearchrec {

// ============================================================
// 索引构建器
// 参考：业界索引构建方案
// ============================================================
class IndexBuilder {
public:
    IndexBuilder() = default;
    ~IndexBuilder() = default;

    // 设置依赖
    void SetInvertedIndex(std::shared_ptr<InvertedIndex> idx) {
        inv_idx_ = idx;
    }
    void SetVectorIndex(std::shared_ptr<VectorIndex> idx) {
        vec_idx_ = idx;
    }
    void SetDocStore(std::shared_ptr<DocStore> store) {
        doc_store_ = store;
    }

    // 从 JSON 文件构建索引
    bool BuildFromJson(const std::string& json_path);

    // 从 Document 列表构建索引
    // persist_to_store: 是否同时写入 DocStore（从 JSON 首次导入时 true，从 SQLite 重建时 false）
    bool BuildFromDocs(const std::vector<Document>& docs, bool persist_to_store = true);

    // 增量添加文档
    bool AddDocument(const Document& doc);

    // 保存索引到磁盘
    bool SaveIndexes(const std::string& index_dir);

    // 从磁盘加载索引
    bool LoadIndexes(const std::string& index_dir);

private:
    std::shared_ptr<InvertedIndex> inv_idx_;
    std::shared_ptr<VectorIndex> vec_idx_;
    std::shared_ptr<DocStore> doc_store_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_INDEX_BUILDER_H
