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
};

} // namespace minisearchrec

#endif // MINISEARCHREC_EVENT_HANDLER_H
