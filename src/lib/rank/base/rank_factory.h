// ============================================================
// MiniSearchRec - 排序工厂基类
// 对标：通用搜索框架 RankFactory
// 通过 business_type 路由到不同的 Factory
// ============================================================

#ifndef MINISEARCHREC_RANK_FACTORY_H
#define MINISEARCHREC_RANK_FACTORY_H

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include "lib/rank/base/rank_item.h"
#include "lib/rank/base/rank_vector.h"
#include "lib/rank/base/rank_context.h"

namespace minisearchrec {
namespace rank {

class Rank;  // forward

// ============================================================
// RankFactory：抽象工厂
// 每个业务实现自己的 Factory（SearchFactory, SugFactory, ...）
// ============================================================
class RankFactory {
public:
    virtual ~RankFactory() = default;

    virtual BaseRankItem* CreateItem() const { return new BaseRankItem(); }
    virtual RankVector* CreateVector() const { return new RankVector(); }
    virtual RankContext* CreateContext() const { return new RankContext(); }
    virtual Rank* CreateRank() const;  // 定义在 rank.cpp
};

// ============================================================
// RankFactory 注册表
// ============================================================
using RankFactoryCreator = std::function<RankFactory*()>;

class RankFactoryRegistry {
public:
    static RankFactoryRegistry& Instance() {
        static RankFactoryRegistry inst;
        return inst;
    }

    void Register(const std::string& name, RankFactoryCreator creator) {
        creators_[name] = std::move(creator);
    }

    RankFactory* GetSingleton(const std::string& name) {
        auto it = singletons_.find(name);
        if (it != singletons_.end()) return it->second.get();

        auto cit = creators_.find(name);
        if (cit == creators_.end()) return nullptr;

        singletons_[name].reset(cit->second());
        return singletons_[name].get();
    }

private:
    RankFactoryRegistry() = default;
    std::unordered_map<std::string, RankFactoryCreator> creators_;
    std::unordered_map<std::string, std::unique_ptr<RankFactory>> singletons_;
};

#define REGISTER_RANK_FACTORY(ClassName) \
    static bool _registered_factory_##ClassName = []() { \
        ::minisearchrec::rank::RankFactoryRegistry::Instance().Register( \
            #ClassName, []() -> ::minisearchrec::rank::RankFactory* { \
                return new ClassName(); \
            }); \
        return true; \
    }()

} // namespace rank
} // namespace minisearchrec

#endif // MINISEARCHREC_RANK_FACTORY_H
