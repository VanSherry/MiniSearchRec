// ============================================================
// MiniSearchRec - HTTP 服务器（路由分发层）
// 所有业务逻辑统一由 biz/ 层处理
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

    bool Initialize();
    void Run();
    void Stop();

private:
    void RegisterRoutes();

    // ── Biz 路由入口 ──
    void HandleSearch(const httplib::Request& req, httplib::Response& res);
    void HandleSug(const httplib::Request& req, httplib::Response& res);
    void HandleHint(const httplib::Request& req, httplib::Response& res);
    void HandleNav(const httplib::Request& req, httplib::Response& res);

    // ── 基础服务 ──
    void HandleAddDoc(const httplib::Request& req, httplib::Response& res);
    void HandleUpdateDoc(const httplib::Request& req, httplib::Response& res);
    void HandleDeleteDoc(const httplib::Request& req, httplib::Response& res);
    void HandleReportEvent(const httplib::Request& req, httplib::Response& res);
    void HandleHealthCheck(const httplib::Request& req, httplib::Response& res);

    std::string host_;
    int port_;
    std::unique_ptr<httplib::Server> server_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_HTTP_SERVER_H
