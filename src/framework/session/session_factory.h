// ============================================================
// MiniSearchRec - SessionFactory
// 对标：通用搜索框架 Session
// 通过 business_type 反射创建对应的 Session 子类实例
// ============================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include "framework/session/session.h"

namespace minisearchrec {
namespace framework {

class SessionFactory {
public:
    static SessionFactory& Instance() {
        static SessionFactory factory;
        return factory;
    }

    // 注册 business_type → session_name 映射
    void Register(uint64_t business_type, const std::string& session_name);

    // 按 string key 注册（兼容字符串 business_type）
    void Register(const std::string& business_type, const std::string& session_name);

    // 创建 Session（通过 business_type 反射）
    std::shared_ptr<Session> CreateSession(uint64_t business_type);
    std::shared_ptr<Session> CreateSession(const std::string& business_type);

    bool HasSession(uint64_t business_type) const;
    bool HasSession(const std::string& business_type) const;

private:
    SessionFactory() = default;

    // business_type(string) → session_name 映射
    std::unordered_map<std::string, std::string> type_session_map_;
};

} // namespace framework
} // namespace minisearchrec
