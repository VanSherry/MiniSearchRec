// ============================================================
// MiniSearchRec - HintFactory + HintRank::PrepareInput 实现
// 对标：ClickHintHandler 的多路召回（MergeRecallProcessor）
// 四路：标签匹配 / 分类热门 / 行为共现 / Query扩展
// ============================================================

#include "biz/hint/hint_factory.h"
#include "biz/hint/hint_rank_item.h"
#include "biz/hint/hint_context.h"
#include "framework/app_context.h"
#include "lib/storage/doc_cooccur_store.h"
#include "lib/storage/query_stats_store.h"
#include "lib/index/doc_store.h"
#include "lib/rank/base/rank_factory.h"
#include "utils/logger.h"
#include <cmath>
#include <ctime>
#include <unordered_set>
#include <algorithm>

namespace minisearchrec {

// Jaccard 相似度
static float TagJaccard(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0f;
    std::unordered_set<std::string> sa(a.begin(), a.end());
    int intersection = 0;
    for (const auto& t : b) {
        if (sa.count(t)) ++intersection;
    }
    return static_cast<float>(intersection) / (sa.size() + b.size() - intersection);
}

int HintRank::PrepareInput() {
    auto hint_ctx = std::static_pointer_cast<HintContext>(ctx_);
    auto vec = ctx_->GetVector();

    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) {
        LOG_ERROR("HintRank::PrepareInput: DocStore not available");
        return -1;
    }

    // 获取源文档信息
    Document src_doc;
    if (!doc_store->GetDoc(hint_ctx->doc_id, src_doc)) {
        LOG_WARN("HintRank::PrepareInput: doc_id='{}' not found", hint_ctx->doc_id);
        return 0;  // 不报错，返回空结果
    }

    hint_ctx->src_title = src_doc.title();
    hint_ctx->src_category = src_doc.category();
    hint_ctx->src_click_count = src_doc.click_count();
    for (const auto& tag : src_doc.tags()) {
        hint_ctx->src_tags.push_back(tag);
    }

    std::unordered_set<std::string> seen;  // 全局去重
    seen.insert(hint_ctx->query);  // 不返回与 query 相同的词
    int64_t now = std::time(nullptr);

    // ── 路1: 标签匹配召回（对标 MergeRecallProcessor tag_match 路）──
    {
        auto all_ids = doc_store->GetAllDocIds();
        for (const auto& id : all_ids) {
            if (id == hint_ctx->doc_id) continue;
            Document doc;
            if (!doc_store->GetDoc(id, doc)) continue;
            std::vector<std::string> tags(doc.tags().begin(), doc.tags().end());
            float overlap = TagJaccard(hint_ctx->src_tags, tags);
            if (overlap > 0.1f && !doc.title().empty() && !seen.count(doc.title())) {
                auto item = std::make_shared<HintRankItem>();
                item->SetWord(doc.title());
                item->text = doc.title();
                item->source = "tag_match";
                item->tag_overlap = overlap;
                item->recall_score = overlap;
                item->AddRetrieveType(1);
                vec->PushBack(item);
                seen.insert(doc.title());
            }
        }
    }

    // ── 路2: 分类热门召回 ──
    if (!hint_ctx->src_category.empty()) {
        auto all_ids = doc_store->GetAllDocIds();
        for (const auto& id : all_ids) {
            if (id == hint_ctx->doc_id) continue;
            Document doc;
            if (!doc_store->GetDoc(id, doc)) continue;
            if (doc.category() == hint_ctx->src_category && !doc.title().empty()
                && !seen.count(doc.title())) {
                float hot = std::log1p(static_cast<float>(doc.click_count())) * 0.6f
                          + std::log1p(static_cast<float>(doc.like_count())) * 0.4f;
                auto item = std::make_shared<HintRankItem>();
                item->SetWord(doc.title());
                item->text = doc.title();
                item->source = "category_hot";
                item->category_score = hot;
                item->recall_score = hot * 0.1f;
                item->AddRetrieveType(2);
                vec->PushBack(item);
                seen.insert(doc.title());
            }
        }
    }

    // ── 路3: 行为共现召回（对标 RECALL_KV_SESSION_V2）──
    {
        auto& cooccur = DocCooccurStore::Instance();
        auto co_items = cooccur.GetTopCooccur(hint_ctx->doc_id, 20);
        for (const auto& ci : co_items) {
            Document doc;
            if (doc_store->GetDoc(ci.dst_doc_id, doc) && !doc.title().empty()
                && !seen.count(doc.title())) {
                auto item = std::make_shared<HintRankItem>();
                item->SetWord(doc.title());
                item->text = doc.title();
                item->source = "cooccur";
                item->cooccur_score = std::log1p(static_cast<float>(ci.co_count));
                item->recall_score = item->cooccur_score * 0.5f;
                item->AddRetrieveType(3);
                vec->PushBack(item);
                seen.insert(doc.title());
            }
        }
    }

    // ── 路4: Query 扩展召回 ──
    if (!hint_ctx->query.empty()) {
        auto& qs = QueryStatsStore::Instance();
        auto prefix = hint_ctx->query.substr(0, std::min((size_t)6, hint_ctx->query.size()));
        auto q_items = qs.GetByPrefix(prefix, 15);
        for (const auto& qi : q_items) {
            if (qi.query != hint_ctx->query && !seen.count(qi.query)) {
                auto item = std::make_shared<HintRankItem>();
                item->SetWord(qi.query);
                item->text = qi.query;
                item->source = "query_expand";
                item->recall_score = std::log1p(static_cast<float>(qi.freq)) * 0.05f;
                item->AddRetrieveType(4);
                vec->PushBack(item);
                seen.insert(qi.query);
            }
        }
    }

    LOG_INFO("HintRank::PrepareInput: doc_id='{}', candidates={}", hint_ctx->doc_id, vec->Size());
    return 0;
}

// ── 注册 HintFactory ──
REGISTER_RANK_FACTORY(HintFactory);

} // namespace minisearchrec
