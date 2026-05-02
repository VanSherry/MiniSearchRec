// ============================================================
// MiniSearchRec - 用户事件处理器实现
// ============================================================

#include "lib/user/user_event_handler.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include <chrono>
#include <algorithm>

namespace minisearchrec {

// 从 DocStore 获取文档类别
static std::string GetDocCategory(const std::string& doc_id) {
    auto store = AppContext::Instance().GetDocStore();
    if (!store) return "";
    Document doc;
    if (!store->GetDoc(doc_id, doc)) return "";
    return doc.category();
}

static void IncreaseCategoryInterest(UserProfile& profile,
                                      const std::string& category,
                                      float delta) {
    if (category.empty()) return;
    auto& w = (*profile.mutable_category_weights())[category];
    w = std::min(1.0f, w + delta);
}

// ============================================================
bool UserEventHandler::HandleEvent(const UserEvent& event,
                                    UserProfile& profile) {
    // proto UserEvent 用 uid() 而非 user_id
    if (event.uid() != profile.uid()) {
        LOG_WARN("UserEventHandler: uid mismatch event.uid={} profile.uid={}",
                 event.uid(), profile.uid());
        return false;
    }

    // proto EventType 枚举值：EVENT_CLICK, EVENT_LIKE 等
    switch (event.event_type()) {
        case EVENT_CLICK:          OnClick(event, profile);   break;
        case EVENT_LIKE:           OnLike(event, profile);    break;
        case EVENT_SHARE:          OnShare(event, profile);   break;
        case EVENT_SEARCH:         OnSearch(event, profile);  break;
        case EVENT_VIEW_DURATION:  OnDwell(event, profile);   break;
        default: break;
    }

    // 更新活跃度
    profile.set_active_days_last_30(
        std::min(30, profile.active_days_last_30() + 1));
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    profile.set_update_time(now);
    return true;
}

int UserEventHandler::HandleEvents(const std::vector<UserEvent>& events,
                                    UserProfile& profile) {
    int handled = 0;
    for (const auto& e : events) {
        if (HandleEvent(e, profile)) handled++;
    }
    return handled;
}

void UserEventHandler::OnClick(const UserEvent& event, UserProfile& profile) {
    auto* ids = profile.mutable_click_doc_ids();
    if (ids->size() >= 200) ids->erase(ids->begin());
    *ids->Add() = event.doc_id();

    profile.set_total_clicks(profile.total_clicks() + 1);
    IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.15f);

    LOG_DEBUG("OnClick uid={} doc={}", event.uid(), event.doc_id());
}

void UserEventHandler::OnView(const UserEvent& event, UserProfile& profile) {
    IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.05f);
}

void UserEventHandler::OnLike(const UserEvent& event, UserProfile& profile) {
    auto* ids = profile.mutable_like_doc_ids();
    if (ids->size() >= 200) ids->erase(ids->begin());
    *ids->Add() = event.doc_id();

    profile.set_total_likes(profile.total_likes() + 1);
    IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.4f);
}

void UserEventHandler::OnCollect(const UserEvent& event, UserProfile& profile) {
    IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.6f);
}

void UserEventHandler::OnShare(const UserEvent& event, UserProfile& profile) {
    IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.5f);
}

void UserEventHandler::OnComment(const UserEvent& event, UserProfile& profile) {
    IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.25f);
}

void UserEventHandler::OnSearch(const UserEvent& event, UserProfile& profile) {
    if (!event.query().empty()) {
        auto* q = profile.mutable_query_history();
        if (q->size() >= 50) q->erase(q->begin());
        *q->Add() = event.query();
    }
}

void UserEventHandler::OnDwell(const UserEvent& event, UserProfile& profile) {
    if (event.view_duration_ms() >= 30000) {
        IncreaseCategoryInterest(profile, GetDocCategory(event.doc_id()), 0.2f);
    }
}

bool UserEventHandler::HandleSessionEvent(const Session& session,
                                           UserProfile& profile) {
    if (!session.qp_info.raw_query.empty()) {
        auto* q = profile.mutable_query_history();
        if (q->size() >= 50) q->erase(q->begin());
        *q->Add() = session.qp_info.raw_query;
    }
    return true;
}

std::string UserEventHandler::EventTypeToString(EventType type) {
    switch (type) {
        case EVENT_CLICK:          return "click";
        case EVENT_LIKE:           return "like";
        case EVENT_SHARE:          return "share";
        case EVENT_SEARCH:         return "search";
        case EVENT_VIEW_DURATION:  return "view";
        case EVENT_DISMISS:        return "dismiss";
        default:                   return "unknown";
    }
}

EventType UserEventHandler::StringToEventType(const std::string& str) {
    if (str == "click")   return EVENT_CLICK;
    if (str == "like")    return EVENT_LIKE;
    if (str == "share")   return EVENT_SHARE;
    if (str == "search")  return EVENT_SEARCH;
    if (str == "view")    return EVENT_VIEW_DURATION;
    if (str == "dismiss") return EVENT_DISMISS;
    return EVENT_UNKNOWN;
}

} // namespace minisearchrec
