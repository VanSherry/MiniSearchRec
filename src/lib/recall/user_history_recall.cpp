// ============================================================
// MiniSearchRec - 用户历史召回处理器实现（DAG 并行模式）
// 参考：X(Twitter) UTEG (User Tweet Entity Graph)
// ============================================================

#include "lib/recall/user_history_recall.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <chrono>
#include <unordered_set>

namespace minisearchrec {

int UserHistoryRecallProcessor::Init(const YAML::Node& config) {
    if (config["enable"]) {
        enabled_ = config["enable"].as<bool>(true);
    }
    if (config["max_recall"]) {
        max_recall_ = config["max_recall"].as<int>(200);
    }
    if (config["history_window_days"]) {
        history_window_days_ = config["history_window_days"].as<int>(30);
    }
    return 0;
}

int UserHistoryRecallProcessor::ProcessDag(framework::DagProcessorContext* ctx) {
    if (!enabled_) return 0;

    auto* ss = dynamic_cast<const SearchSession*>(ctx->session);
    if (!ss) return -1;

    const auto& profile_ptr = ss->user_profile;
    if (!profile_ptr || profile_ptr->uid().empty()) {
        return 0;
    }
    const auto& profile = *profile_ptr;

    std::unordered_set<std::string> seen_ids;
    std::vector<std::string> history_doc_ids;
    for (const auto& doc_id : profile.click_doc_ids()) {
        if (seen_ids.insert(doc_id).second) {
            history_doc_ids.push_back(doc_id);
        }
    }
    for (const auto& doc_id : profile.like_doc_ids()) {
        if (seen_ids.insert(doc_id).second) {
            history_doc_ids.push_back(doc_id);
        }
    }

    const auto& query_terms = ss->qp_info.terms;
    const std::string& inferred_category = ss->qp_info.inferred_category;

    auto doc_store = AppContext::Instance().GetDocStore();

    std::vector<DocCandidate> candidates;

    int count = 0;
    for (const auto& doc_id : history_doc_ids) {
        if (count >= max_recall_) break;

        DocCandidate cand;
        cand.doc_id = doc_id;
        cand.recall_source = "user_history";

        if (doc_store) {
            Document doc;
            if (!doc_store->GetDoc(doc_id, doc)) continue;

            if (!query_terms.empty()) {
                bool relevant = false;
                const std::string& title = doc.title();
                const std::string& category = doc.category();
                for (const auto& term : query_terms) {
                    if (title.find(term) != std::string::npos ||
                        category.find(term) != std::string::npos) {
                        relevant = true;
                        break;
                    }
                }
                if (!relevant && !inferred_category.empty() &&
                    category == inferred_category) {
                    relevant = true;
                }
                if (!relevant) continue;
            }

            cand.title           = doc.title();
            cand.content_snippet = utils::Utf8Truncate(doc.content(), 200);
            cand.author          = doc.author();
            cand.publish_time    = doc.publish_time();
            cand.category        = doc.category();
            cand.quality_score   = doc.quality_score();
            cand.click_count     = doc.click_count();
            cand.like_count      = doc.like_count();
        }

        cand.recall_score = 1.0f / (count + 1);
        ctx->output->doc_scores.emplace_back(doc_id, cand.recall_score);
        candidates.push_back(std::move(cand));
        count++;
    }

    ctx->output->items = std::move(candidates);
    ctx->output->item_count = count;

    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(UserHistoryRecallProcessor);
