// ============================================================
// MiniSearchRec - NavFactory + NavRank::PrepareInput 实现
// 对标：QXNavHotSearchHandler 的召回逻辑
// ============================================================

#include "biz/nav/nav_factory.h"
#include "biz/nav/nav_rank_item.h"
#include "biz/nav/nav_context.h"
#include "framework/app_context.h"
#include "lib/storage/query_stats_store.h"
#include "lib/index/doc_store.h"
#include "lib/rank/base/rank_factory.h"
#include "utils/logger.h"
#include <cmath>
#include <ctime>
#include <unordered_set>

namespace minisearchrec {

int NavRank::PrepareInput() {
    auto nav_ctx = std::static_pointer_cast<NavContext>(ctx_);
    auto vec = ctx_->GetVector();
    int64_t now = std::time(nullptr);
    std::unordered_set<std::string> seen;

    // ── 路1: 全局热词（从 QueryStatsStore）──
    {
        auto& qs = QueryStatsStore::Instance();
        auto top_queries = qs.GetTopN(30);
        for (const auto& q : top_queries) {
            if (q.query.empty() || seen.count(q.query)) continue;
            float days = static_cast<float>(now - q.last_time) / 86400.0f;
            float hot = static_cast<float>(q.freq) * std::exp(-nav_ctx->decay_rate * std::max(0.0f, days));

            auto item = std::make_shared<NavRankItem>();
            item->SetWord(q.query);
            item->source = "hot";
            item->hot_score = hot;
            item->click_count = q.freq;
            item->publish_time = q.last_time;
            item->SetScore(hot);
            vec->PushBack(item);
            seen.insert(q.query);
        }
    }

    // ── 路2: 文档标题热词 ──
    {
        auto doc_store = AppContext::Instance().GetDocStore();
        if (doc_store) {
            auto doc_ids = doc_store->GetAllDocIds();
            for (const auto& id : doc_ids) {
                Document doc;
                if (!doc_store->GetDoc(id, doc)) continue;
                if (doc.title().empty() || seen.count(doc.title())) continue;

                float days = static_cast<float>(now - doc.publish_time()) / 86400.0f;
                float hot = static_cast<float>(doc.click_count()) *
                           std::exp(-nav_ctx->decay_rate * std::max(0.0f, days));

                auto item = std::make_shared<NavRankItem>();
                item->SetWord(doc.title());
                item->source = "doc_hot";
                item->hot_score = hot;
                item->click_count = doc.click_count();
                item->publish_time = doc.publish_time();
                item->SetScore(hot);
                vec->PushBack(item);
                seen.insert(doc.title());
            }
        }
    }

    // ── 路3: 预置词兜底（冷启动）──
    {
        for (const auto& pw : nav_ctx->preset_words) {
            if (!seen.count(pw)) {
                auto item = std::make_shared<NavRankItem>();
                item->SetWord(pw);
                item->source = "preset";
                item->hot_score = 0.1f;
                item->SetScore(0.1f);
                vec->PushBack(item);
                seen.insert(pw);
            }
        }
    }

    LOG_INFO("NavRank::PrepareInput: candidates={}", vec->Size());
    return 0;
}

// ── 注册 NavFactory ──
REGISTER_RANK_FACTORY(NavFactory);

} // namespace minisearchrec
