// ============================================================
// MiniSearchRec - SessionFactory 实现
// ============================================================

#include "framework/session/session_factory.h"
#include "framework/class_register.h"
#include "utils/logger.h"

namespace minisearchrec {
namespace framework {

void SessionFactory::Register(uint64_t business_type, const std::string& session_name) {
    Register(std::to_string(business_type), session_name);
}

void SessionFactory::Register(const std::string& business_type, const std::string& session_name) {
    if (type_session_map_.count(business_type)) {
        LOG_WARN("SessionFactory: business_type '{}' already registered, overwriting", business_type);
    }
    type_session_map_[business_type] = session_name;
    LOG_INFO("SessionFactory: registered business_type='{}' → session='{}'", business_type, session_name);
}

std::shared_ptr<Session> SessionFactory::CreateSession(uint64_t business_type) {
    return CreateSession(std::to_string(business_type));
}

std::shared_ptr<Session> SessionFactory::CreateSession(const std::string& business_type) {
    auto it = type_session_map_.find(business_type);
    if (it == type_session_map_.end()) {
        // 未注册的 business_type，返回默认 Session
        LOG_DEBUG("SessionFactory: no session for '{}', creating default Session", business_type);
        return std::make_shared<Session>();
    }

    auto session = ClassRegistry<Session>::Instance().Create(it->second);
    if (!session) {
        LOG_ERROR("SessionFactory: failed to create session '{}' for business_type '{}'",
                  it->second, business_type);
        return std::make_shared<Session>();
    }
    return session;
}

bool SessionFactory::HasSession(uint64_t business_type) const {
    return HasSession(std::to_string(business_type));
}

bool SessionFactory::HasSession(const std::string& business_type) const {
    return type_session_map_.find(business_type) != type_session_map_.end();
}

} // namespace framework
} // namespace minisearchrec
