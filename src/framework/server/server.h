// ============================================================
// MiniSearchRec - Server 主入口
// 对标：通用搜索框架 Server
//
// Server 是整个搜索服务的核心调度器：
//   1. Init(): 初始化所有子系统（SessionFactory, HandlerMgr, RankMgr 等）
//   2. Search(): 统一请求入口
//      → SessionFactory 创建 Session
//      → HandlerMgr 路由到 Handler
//      → Handler::Search(session) 执行主流程
//   3. Stop(): 优雅退出
// ============================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "framework/session/session.h"
#include "httplib.h"

namespace minisearchrec {
namespace framework {

class Server {
public:
    static Server& Instance() {
        static Server instance;
        return instance;
    }

    // 服务初始化（在 main 中调用，初始化所有子系统）
    int32_t Init();

    // 统一搜索入口（对标 Server::Search(uin, req, resp)）
    // 由 HTTP 路由层调用，business_type 自动路由到对应 Handler
    int32_t Search(const std::string& business_type,
                   const CommonRequest& request,
                   CommonResponse& response);

    // 便捷方法：直接从 httplib 请求中提取参数并路由
    void HandleRequest(const std::string& business_type,
                       const httplib::Request& req,
                       httplib::Response& res);

    // 停止服务
    void Stop();

private:
    Server() = default;
    ~Server() = default;

    bool initialized_ = false;
};

} // namespace framework
} // namespace minisearchrec
