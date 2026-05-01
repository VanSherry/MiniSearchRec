// ============================================================
// MiniSearchRec - 搜索接口处理器
// 对应 API：POST /api/v1/search
// ============================================================

#ifndef MINISEARCHREC_SEARCH_HANDLER_H
#define MINISEARCHREC_SEARCH_HANDLER_H

#include <string>
#include "httplib.h"
#include "search.pb.h"

namespace minisearchrec {

class SearchHandler {
public:
    SearchHandler() = default;

    // 处理搜索请求
    void Handle(const httplib::Request& req, httplib::Response& res);

private:
    // 解析请求体
    bool ParseRequest(const std::string& body, SearchRequest& request);

    // 构建 Session 并执行 Pipeline
    int DoSearch(const SearchRequest& request, SearchResponse& response);

    // 序列化响应
    std::string SerializeResponse(const SearchResponse& response);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_SEARCH_HANDLER_H
