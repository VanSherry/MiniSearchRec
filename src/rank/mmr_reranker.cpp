// ============================================================
// MiniSearchRec - MMR 多样性重排实现
// 参考工业标准多样性算法
// ============================================================

#include "rank/mmr_reranker.h"
#include <algorithm>
#include <cmath>

namespace minisearchrec {

bool MMRRerankProcessor::Init(const YAML::Node& config) {
    if (config["lambda"]) {
        lambda_ = config["lambda"].as<float>(0.7f);
    }
    if (config["top_k"]) {
        top_k_ = config["top_k"].as<int>(20);
    }
    if (config["max_candidates"]) {
        max_candidates_ = config["max_candidates"].as<int>(100);
    }
    return true;
}

float MMRRerankProcessor::CalcSimilarity(const DocCandidate& a,
                                             const DocCandidate& b) {
    // 简化版：基于标题的 Jaccard 相似度
    if (a.title.empty() || b.title.empty()) return 0.0f;

    std::set<std::string> tokens_a;
    std::set<std::string> tokens_b;

    // 简单按空格/标点分词
    std::string current;
    for (char c : a.title) {
        if (std::isspace(c) || std::ispunct(c)) {
            if (!current.empty()) tokens_a.insert(current);
            current.clear();
        } else {
            current += ::tolower(c);
        }
    }
    if (!current.empty()) tokens_a.insert(current);

    current.clear();
    for (char c : b.title) {
        if (std::isspace(c) || std::ispunct(c)) {
            if (!current.empty()) tokens_b.insert(current);
            current.clear();
        } else {
            current += ::tolower(c);
        }
    }
    if (!current.empty()) tokens_b.insert(current);

    std::set<std::string> intersection, uni;
    std::set_intersection(tokens_a.begin(), tokens_a.end(),
                          tokens_b.begin(), tokens_b.end(),
                          std::inserter(intersection, intersection.begin()));
    std::set_union(tokens_a.begin(), tokens_a.end(),
                    tokens_b.begin(), tokens_b.end(),
                    std::inserter(uni, uni.begin()));

    if (uni.empty()) return 0.0f;
    return static_cast<float>(intersection.size()) / uni.size();
}

int MMRRerankProcessor::Process(Session& session,
                                  std::vector<DocCandidate>& candidates) {
    if (candidates.empty()) return 0;

    // 限制参与重排的候选数
    int num_candidates = std::min(max_candidates_, (int)candidates.size());
    std::vector<DocCandidate> input(candidates.begin(),
                                     candidates.begin() + num_candidates);

    std::vector<DocCandidate> selected;
    std::vector<bool> used(input.size(), false);

    // 第一次选择：直接选相关性最高的
    if (!input.empty()) {
        int best_idx = 0;
        float best_score = -1e9f;
        for (int i = 0; i < (int)input.size(); ++i) {
            float score = input[i].fine_score > 0 ? input[i].fine_score
                                                  : input[i].coarse_score;
            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }
        selected.push_back(input[best_idx]);
        used[best_idx] = true;
    }

    // 后续选择：MMR 准则
    while ((int)selected.size() < top_k_ && (int)selected.size() < (int)input.size()) {
        float best_mmr = -1e9f;
        int best_idx = -1;

        for (int i = 0; i < (int)input.size(); ++i) {
            if (used[i]) continue;

            float relevance = input[i].fine_score > 0 ? input[i].fine_score
                                                     : input[i].coarse_score;

            // 计算与已选文档的最大相似度
            float max_sim = 0.0f;
            for (const auto& sel : selected) {
                float sim = CalcSimilarity(input[i], sel);
                max_sim = std::max(max_sim, sim);
            }

            float mmr = lambda_ * relevance - (1.0f - lambda_) * max_sim;

            if (mmr > best_mmr) {
                best_mmr = mmr;
                best_idx = i;
            }
        }

        if (best_idx >= 0) {
            selected.push_back(input[best_idx]);
            used[best_idx] = true;
        } else {
            break;
        }
    }

    // 更新候选列表
    candidates = std::move(selected);
    return 0;
}

} // namespace minisearchrec
