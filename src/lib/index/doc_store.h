// ============================================================
// MiniSearchRec - 文档存储（SQLite 封装）
// 负责文档的持久化存储和查询
//
// 线程安全策略：
//   - 读操作（GetDoc/GetAllDocIds/GetDocCount）：使用 thread_local
//     独立连接，每个线程各有自己的 sqlite3* 句柄，SQLite WAL 模式
//     天然支持多连接并发读，无需加锁
//   - 写操作（PutDoc/PutDocs/DeleteDoc）：使用 shared_mutex 独占保护，
//     同时持有写连接的唯一使用权
// ============================================================

#ifndef MINISEARCHREC_DOC_STORE_H
#define MINISEARCHREC_DOC_STORE_H

#include <string>
#include <vector>
#include <memory>
#include "doc.pb.h"

// 前向声明，避免头文件暴露 sqlite3.h
struct sqlite3;

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

    // 添加/更新文档（写操作，独占锁）
    bool PutDoc(const Document& doc);

    // 批量添加文档（写操作，独占锁）
    bool PutDocs(const std::vector<Document>& docs);

    // 获取文档（读操作，thread_local 连接，无锁并发读）
    bool GetDoc(const std::string& doc_id, Document& doc);

    // 删除文档（写操作，独占锁）
    bool DeleteDoc(const std::string& doc_id);

    // 获取所有文档 ID（读操作，thread_local 连接，无锁并发读）
    std::vector<std::string> GetAllDocIds();

    // 获取文档总数（读操作，thread_local 连接，无锁并发读）
    int64_t GetDocCount();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    // 获取当前线程的读连接（懒创建，thread_local）
    sqlite3* GetReadConn();
};

} // namespace minisearchrec

#endif // MINISEARCHREC_DOC_STORE_H
