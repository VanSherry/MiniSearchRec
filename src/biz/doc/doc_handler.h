// ============================================================
// MiniSearchRec - 文档管理接口处理器
// 对应 API：POST /api/v1/doc/add, PUT /api/v1/doc/update, DELETE /api/v1/doc/delete
// ============================================================

#ifndef MINISEARCHREC_DOC_HANDLER_H
#define MINISEARCHREC_DOC_HANDLER_H

#include <string>
#include "httplib.h"

namespace minisearchrec {

class DocHandler {
public:
    DocHandler() = default;

    // 处理添加文档
    void HandleAdd(const httplib::Request& req, httplib::Response& res);

    // 处理更新文档
    void HandleUpdate(const httplib::Request& req, httplib::Response& res);

    // 处理删除文档
    void HandleDelete(const httplib::Request& req, httplib::Response& res);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_DOC_HANDLER_H
