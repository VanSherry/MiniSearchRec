// ============================================================
// MiniSearchRec - 用户行为上报接口处理器
// 对应 API：POST /api/v1/event/click, /api/v1/event/like
// ============================================================

#ifndef MINISEARCHREC_EVENT_HANDLER_H
#define MINISEARCHREC_EVENT_HANDLER_H

#include <string>
#include "httplib.h"

namespace minisearchrec {

class EventHandler {
public:
    EventHandler() = default;

    // 处理用户行为上报
    void Handle(const httplib::Request& req, httplib::Response& res);

    // 查询事件数据（用于管理后台）
    // 返回 JSON 格式的事件列表
    static std::string QueryEventsAsJson(const std::string& uid,
                                         const std::string& event_type,
                                         int limit,
                                         int64_t since_ts);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_EVENT_HANDLER_H
