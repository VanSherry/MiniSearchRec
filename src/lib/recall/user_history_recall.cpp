// ============================================================
// MiniSearchRec - 用户历史召回处理器实现
// 参考：X(Twitter) UTEG (User Tweet Entity Graph)
// ============================================================

#include "lib/recall/user_history_recall.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <chrono>
#include <unordered_set>

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

    // 获取用户历史点击/点赞的文档（内部先去重）
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

    // 获取 query 分词，用于相关性过滤
    const auto& query_terms = session.qp_info.terms;
    const std::string& inferred_category = session.qp_info.inferred_category;

    auto doc_store = AppContext::Instance().GetDocStore();

    // 构建已有 doc_id 集合，O(1) 去重
    std::unordered_set<std::string> existing_ids;
    for (const auto& cand : session.recall_results) {
        existing_ids.insert(cand.doc_id);
    }

    int count = 0;
    for (const auto& doc_id : history_doc_ids) {
        if (count >= max_recall_) break;
        if (existing_ids.count(doc_id)) continue;

        DocCandidate cand;
        cand.doc_id = doc_id;
        cand.recall_source = "user_history";

        if (doc_store) {
            Document doc;
            if (!doc_store->GetDoc(doc_id, doc)) continue;

            // Query 相关性过滤：若有 query 分词，则文档需命中至少一个 term 或同类别
            if (!query_terms.empty()) {
                bool relevant = false;
                // 检查标题/类别是否含 query 词
                const std::string& title = doc.title();
                const std::string& category = doc.category();
                for (const auto& term : query_terms) {
                    if (title.find(term) != std::string::npos ||
                        category.find(term) != std::string::npos) {
                        relevant = true;
                        break;
                    }
                }
                // 类别匹配也算相关
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
        session.recall_results.push_back(cand);
        existing_ids.insert(doc_id);
        count++;
    }

    session.counts.recall_source_counts["user_history"] = count;
    return 0;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
REGISTER_MSR_PROCESSOR(UserHistoryRecallProcessor);
