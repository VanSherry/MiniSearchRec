// ============================================================
// MiniSearchRec - HTTP 服务器
// 使用 cpp-httplib（header-only）
// ============================================================

#ifndef MINISEARCHREC_HTTP_SERVER_H
#define MINISEARCHREC_HTTP_SERVER_H

#include <memory>
#include <string>
#include "httplib.h"

namespace minisearchrec {

class HttpServer {
public:
    explicit HttpServer(const std::string& host, int port);
    ~HttpServer();

    // 初始化服务器，注册路由
    bool Initialize();

    // 运行服务器（阻塞）
    void Run();

    // 停止服务器
    void Stop();

private:
    // 注册所有路由
    void RegisterRoutes();

    // 搜索接口
    void HandleSearch(const httplib::Request& req, httplib::Response& res);

    // 文档管理接口
    void HandleAddDoc(const httplib::Request& req, httplib::Response& res);
    void HandleUpdateDoc(const httplib::Request& req, httplib::Response& res);
    void HandleDeleteDoc(const httplib::Request& req, httplib::Response& res);

    // 用户行为接口
    void HandleReportEvent(const httplib::Request& req, httplib::Response& res);

    // 健康检查
    void HandleHealthCheck(const httplib::Request& req, httplib::Response& res);

    std::string host_;
    int port_;
    std::unique_ptr<httplib::Server> server_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HTTP_SERVER_H
