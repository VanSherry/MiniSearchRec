// ============================================================
// MiniSearchRec - 去重过滤器实现
// 基于标题相似度去重
// ============================================================

#include "filter/dedup_filter.h"
#include <algorithm>

namespace minisearchrec {

bool DedupFilterProcessor::Init(const YAML::Node& config) {
    if (config["similarity_threshold"]) {
        similarity_threshold_ = config["similarity_threshold"].as<float>(0.9f);
    }
    return true;
}

// 简易字符串相似度（Jaccard 相似度基于分词）
static float CalcSimilarity(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 1.0f;
    if (a.empty() || b.empty()) return 0.0f;

    // 转为小写后比较
    std::string la = a, lb = b;
    std::transform(la.begin(), la.end(), la.begin(), ::tolower);
    std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);

    // 简单字符级 Jaccard
    std::set<char> set_a(la.begin(), la.end());
    std::set<char> set_b(lb.begin(), lb.end());

    std::set<char> intersection, uni;
    std::set_intersection(set_a.begin(), set_a.end(),
                          set_b.begin(), set_b.end(),
                          std::inserter(intersection, intersection.begin()));
    std::set_union(set_a.begin(), set_a.end(),
                    set_b.begin(), set_b.end(),
                    std::inserter(uni, uni.begin()));

    if (uni.empty()) return 1.0f;
    return static_cast<float>(intersection.size()) / uni.size();
}

bool DedupFilterProcessor::ShouldKeep(const Session& session,
                                       const DocCandidate& candidate) {
    // 与已保留的结果比较标题相似度
    for (const auto& kept : session.final_results) {
        float sim = CalcSimilarity(candidate.title, kept.title);
        if (sim >= similarity_threshold_) {
            return false;  // 相似度过高，过滤掉
        }
    }
    return true;
}

} // namespace minisearchrec
