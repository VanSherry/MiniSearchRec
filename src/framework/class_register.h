// ============================================================
// MiniSearchRec - 类注册宏
// 通用反射注册框架
// 提供编译期注册 + 运行时反射创建能力
// ============================================================

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace minisearchrec {
namespace framework {

// ============================================================
// 通用注册表模板
// ============================================================
template <typename Base>
class ClassRegistry {
public:
    using Creator = std::function<Base*()>;
    using SharedCreator = std::function<std::shared_ptr<Base>()>;

    static ClassRegistry& Instance() {
        static ClassRegistry registry;
        return registry;
    }

    // 注册单例模式（Handler 用：全局一个实例）
    void RegisterSingleton(const std::string& name, Creator creator) {
        singleton_creators_[name] = std::move(creator);
    }

    // 注册每次创建模式（Session 用：每请求一个实例）
    void RegisterCreator(const std::string& name, SharedCreator creator) {
        shared_creators_[name] = std::move(creator);
    }

    // 获取单例（Handler）
    Base* GetSingleton(const std::string& name) {
        auto it = singletons_.find(name);
        if (it != singletons_.end()) {
            return it->second;
        }
        auto cit = singleton_creators_.find(name);
        if (cit != singleton_creators_.end()) {
            Base* instance = cit->second();
            singletons_[name] = instance;
            return instance;
        }
        return nullptr;
    }

    // 每次创建新实例（Session）
    std::shared_ptr<Base> Create(const std::string& name) {
        auto it = shared_creators_.find(name);
        if (it != shared_creators_.end()) {
            return it->second();
        }
        return nullptr;
    }

    bool HasSingleton(const std::string& name) const {
        return singleton_creators_.find(name) != singleton_creators_.end();
    }

    bool HasCreator(const std::string& name) const {
        return shared_creators_.find(name) != shared_creators_.end();
    }

private:
    ClassRegistry() = default;
    std::unordered_map<std::string, Creator> singleton_creators_;
    std::unordered_map<std::string, SharedCreator> shared_creators_;
    std::unordered_map<std::string, Base*> singletons_;  // 懒初始化单例缓存
};

} // namespace framework
} // namespace minisearchrec

// ============================================================
// 注册宏
// ============================================================

// Handler 注册宏（单例模式）
// 用法：在 .cpp 中 REGISTER_MSR_HANDLER(SugBizHandler)
#define REGISTER_MSR_HANDLER(HandlerClass)                                          \
    namespace {                                                                     \
    struct HandlerClass##_Registrar {                                               \
        HandlerClass##_Registrar() {                                                \
            ::minisearchrec::framework::ClassRegistry<                              \
                ::minisearchrec::framework::BaseHandler>::Instance()                \
                .RegisterSingleton(#HandlerClass, []() -> ::minisearchrec::framework::BaseHandler* { \
                    static HandlerClass instance;                                   \
                    return &instance;                                               \
                });                                                                 \
        }                                                                           \
    };                                                                              \
    static HandlerClass##_Registrar g_##HandlerClass##_registrar;                  \
    }

// Session 注册宏（每请求创建模式）
// 用法：在 .cpp 中 REGISTER_MSR_SESSION(SugSession)
#define REGISTER_MSR_SESSION(SessionClass)                                          \
    namespace {                                                                     \
    struct SessionClass##_Registrar {                                               \
        SessionClass##_Registrar() {                                                \
            ::minisearchrec::framework::ClassRegistry<                              \
                ::minisearchrec::framework::Session>::Instance()                    \
                .RegisterCreator(#SessionClass, []() -> std::shared_ptr<::minisearchrec::framework::Session> { \
                    return std::make_shared<SessionClass>();                        \
                });                                                                 \
        }                                                                           \
    };                                                                              \
    static SessionClass##_Registrar g_##SessionClass##_registrar;                  \
    }
