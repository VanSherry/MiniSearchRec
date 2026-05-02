// ============================================================
// MiniSearchRec - 文档存储（SQLite 封装）
// 负责文档的持久化存储和查询
// ============================================================

#ifndef MINISEARCHREC_DOC_STORE_H
#define MINISEARCHREC_DOC_STORE_H

#include <string>
#include <vector>
#include <memory>
#include "doc.pb.h"

namespace minisearchrec {

// ============================================================
// 文档存储接口
// 参考：业界文档持久化存储
// ============================================================
class DocStore {
public:
    DocStore();
    ~DocStore();

    // 打开/创建数据库
    bool Open(const std::string& db_path);

    // 关闭数据库
    void Close();

    // 添加/更新文档
    bool PutDoc(const Document& doc);

    // 批量添加文档
    bool PutDocs(const std::vector<Document>& docs);

    // 获取文档
    bool GetDoc(const std::string& doc_id, Document& doc);

    // 删除文档
    bool DeleteDoc(const std::string& doc_id);

    // 获取所有文档 ID
    std::vector<std::string> GetAllDocIds();

    // 获取文档总数
    int64_t GetDocCount();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_DOC_STORE_H
