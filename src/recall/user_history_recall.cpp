// ============================================================
// MiniSearchRec - 用户历史召回处理器实现
// 参考：X(Twitter) UTEG (User Tweet Entity Graph)
// ============================================================

#include "recall/user_history_recall.h"
#include "core/app_context.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <chrono>

namespace minisearchrec {

bool UserHistoryRecallProcessor::Init(const YAML::Node& config) {
    if (config["enable"]) {
        enabled_ = config["enable"].as<bool>(true);
    }
    if (config["max_recall"]) {
        max_recall_ = config["max_recall"].as<int>(200);
    }
    if (config["history_window_days"]) {
        history_window_days_ = config["history_window_days"].as<int>(30);
    }
    return true;
}

int UserHistoryRecallProcessor::Process(Session& session) {
    if (!enabled_) return 0;

    // user_profile 是 unique_ptr<UserProfile>
    const auto& profile_ptr = session.user_profile;
    if (!profile_ptr || profile_ptr->uid().empty()) {
        // 无用户上下文，跳过
        return 0;
    }
    const auto& profile = *profile_ptr;

    // 获取用户历史点击/点赞的文档
    std::vector<std::string> history_doc_ids;
    for (const auto& doc_id : profile.click_doc_ids()) {
        history_doc_ids.push_back(doc_id);
    }
    for (const auto& doc_id : profile.like_doc_ids()) {
        // 去重
        if (std::find(history_doc_ids.begin(), history_doc_ids.end(), doc_id)
                == history_doc_ids.end()) {
            history_doc_ids.push_back(doc_id);
        }
    }

    auto doc_store = AppContext::Instance().GetDocStore();

    int count = 0;
    for (const auto& doc_id : history_doc_ids) {
        if (count >= max_recall_) break;

        bool exists = false;
        for (const auto& cand : session.recall_results) {
            if (cand.doc_id == doc_id) { exists = true; break; }
        }

        if (!exists) {
            DocCandidate cand;
            cand.doc_id = doc_id;
            cand.recall_source = "user_history";
            cand.recall_score = 1.0f / (count + 1);

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
            count++;
        }
    }

    session.counts.recall_source_counts["user_history"] = count;
    return 0;
}

} // namespace minisearchrec
