// ============================================================
// MiniSearchRec - 倒排索引召回处理器实现
// ============================================================

#include "lib/recall/inverted_recall.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <algorithm>

namespace minisearchrec {

int InvertedRecallProcessor::Init(const YAML::Node& config) {
    if (config["enable"]) {
        enabled_ = config["enable"].as<bool>(true);
    }
    if (config["max_recall"]) {
        max_recall_ = config["max_recall"].as<int>(1000);
    }
    if (config["min_term_freq"]) {
        min_term_freq_ = config["min_term_freq"].as<int>(1);
    }
    index_ = AppContext::Instance().GetInvertedIndex();
    if (!index_) {
        LOG_WARN("InvertedRecallProcessor: InvertedIndex not ready in AppContext");
    }
    return 0;
}

int InvertedRecallProcessor::Process(Session& session) {
    if (!enabled_) return 0;
    if (!index_) {
        index_ = AppContext::Instance().GetInvertedIndex();
        if (!index_) {
            LOG_WARN("InvertedRecallProcessor: index not available, skipping");
            return 0;
        }
    }

    const auto& terms = session.qp_info.terms;
    if (terms.empty()) {
        return 0;
    }

    LOG_DEBUG("InvertedRecall: terms={}, doc_count={}, term_count={}",
              terms.size(), index_->GetDocCount(), index_->GetTermCount());

    auto doc_ids = index_->Search(terms, max_recall_);
    session.search_counts.recall_source_counts["inverted_index"] = doc_ids.size();

    LOG_INFO("InvertedRecall: found {} docs for query terms={}", doc_ids.size(), terms.size());

    // 获取 DocStore 用于填充文档元数据
    auto doc_store = AppContext::Instance().GetDocStore();

    for (const auto& doc_id : doc_ids) {
        DocCandidate cand;
        cand.doc_id = doc_id;
        cand.recall_source = "inverted_index";

        // 计算召回分数（基于命中词数和 IDF）
        auto postings = index_->GetDocPostings(doc_id);
        float recall_score = 0.0f;
        for (const auto& term : terms) {
            auto it = postings.find(term);
            if (it != postings.end()) {
                float idf = index_->CalculateIDF(term);
                recall_score += (1.0f + std::log1pf(it->second.term_freq)) * idf;
            }
        }
        cand.recall_score = recall_score;

        // 从 DocStore 填充文档元数据
        if (doc_store) {
            Document doc;
            if (doc_store->GetDoc(doc_id, doc)) {
                cand.title           = doc.title();
                cand.content_snippet = utils::Utf8Truncate(doc.content(), 200);
                cand.author          = doc.author();
                cand.publish_time    = doc.publish_time();
                cand.category        = doc.category();
                cand.quality_score   = doc.quality_score();
                cand.click_count     = doc.click_count();
                cand.like_count      = doc.like_count();
            }
        }

        session.recall_results.push_back(cand);
    }

    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(InvertedRecallProcessor);
