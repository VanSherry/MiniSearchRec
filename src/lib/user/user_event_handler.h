// ============================================================
// MiniSearchRec - 用户事件处理器
// 参考：业界用户事件处理
// 使用 proto 定义的 UserEvent / EventType / UserProfile
// ============================================================

#ifndef MINISEARCHREC_USER_EVENT_HANDLER_H
#define MINISEARCHREC_USER_EVENT_HANDLER_H

#include <string>
#include <vector>
#include <cstdint>
#include "biz/search/search_session.h"
#include "user_profile.h"
// proto 生成的 UserEvent / EventType
#include "user.pb.h"

namespace minisearchrec {

// ============================================================
// 用户事件处理器
// 将 proto UserEvent 更新到 UserProfile
// ============================================================
class UserEventHandler {
public:
    UserEventHandler() = default;
    ~UserEventHandler() = default;

    // 处理单个 proto UserEvent
    bool HandleEvent(const ::minisearchrec::UserEvent& event,
                     ::minisearchrec::UserProfile& profile);

    // 批量处理
    int HandleEvents(const std::vector<::minisearchrec::UserEvent>& events,
                     ::minisearchrec::UserProfile& profile);

    // 从 Session 中提取搜索 query 更新画像
    bool HandleSessionEvent(const Session& session,
                            ::minisearchrec::UserProfile& profile);

    // 事件类型 <-> 字符串互转
    static std::string EventTypeToString(::minisearchrec::EventType type);
    static ::minisearchrec::EventType StringToEventType(const std::string& str);

private:
    void OnClick  (const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnView   (const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnLike   (const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnCollect(const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnShare  (const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnComment(const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnSearch (const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
    void OnDwell  (const ::minisearchrec::UserEvent& event,
                   ::minisearchrec::UserProfile& profile);
};

} // namespace minisearchrec

#endif // MINISEARCHREC_USER_EVENT_HANDLER_H
