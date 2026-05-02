// ============================================================
// MiniSearchRec - 去重过滤器实现
// 基于标题相似度去重
// ============================================================

#include "lib/filter/dedup_filter.h"
#include <algorithm>

namespace minisearchrec {

int DedupFilterProcessor::Init(const YAML::Node& config) {
    if (config["similarity_threshold"]) {
        similarity_threshold_ = config["similarity_threshold"].as<float>(0.9f);
    }
    return 0;
}

// UTF-8 字符级 Jaccard 相似度（支持中文）
static float CalcSimilarity(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 1.0f;
    if (a.empty() || b.empty()) return 0.0f;

    // 提取 UTF-8 字符集合
    auto extract_chars = [](const std::string& s) -> std::set<std::string> {
        std::set<std::string> chars;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = 1;
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            else if (std::isspace(c) || std::ispunct(c)) { ++i; continue; }
            else {
                // ASCII: 转小写
                chars.insert(std::string(1, static_cast<char>(std::tolower(c))));
                ++i;
                continue;
            }
            if (i + len <= s.size()) {
                chars.insert(s.substr(i, len));
            }
            i += len;
        }
        return chars;
    };

    auto set_a = extract_chars(a);
    auto set_b = extract_chars(b);

    if (set_a.empty() || set_b.empty()) return 0.0f;

    std::set<std::string> intersection, uni;
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

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(DedupFilterProcessor);
