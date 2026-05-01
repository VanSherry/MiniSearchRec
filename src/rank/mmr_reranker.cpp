// ============================================================
// MiniSearchRec - MMR 多样性重排实现
// 参考工业标准多样性算法
// ============================================================

#include "rank/mmr_reranker.h"
#include "utils/logger.h"
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
    // 支持中文的标题相似度：
    //   - 英文/数字：按空格/标点分词，得到词级 token
    //   - 中文：按 UTF-8 字符逐字切分为单字符 token
    // 两者混合放入 set，再计算 Jaccard 相似度
    if (a.title.empty() || b.title.empty()) return 0.0f;

    // 提取 UTF-8 字符串中的 token set（中文逐字，英文按词）
    auto extract_tokens = [](const std::string& s) -> std::set<std::string> {
        std::set<std::string> tokens;
        std::string cur_word;
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                // ASCII 字节
                if (std::isspace(c) || std::ispunct(c)) {
                    if (!cur_word.empty()) {
                        tokens.insert(cur_word);
                        cur_word.clear();
                    }
                    ++i;
                } else {
                    cur_word += static_cast<char>(::tolower(c));
                    ++i;
                }
            } else {
                // 非 ASCII：先 flush 当前英文词
                if (!cur_word.empty()) {
                    tokens.insert(cur_word);
                    cur_word.clear();
                }
                // 确定 UTF-8 多字节长度
                int char_len = 1;
                if ((c & 0xE0) == 0xC0) char_len = 2;
                else if ((c & 0xF0) == 0xE0) char_len = 3;
                else if ((c & 0xF8) == 0xF0) char_len = 4;
                if (i + char_len <= s.size()) {
                    tokens.insert(s.substr(i, char_len));
                }
                i += char_len;
            }
        }
        if (!cur_word.empty()) tokens.insert(cur_word);
        return tokens;
    };

    std::set<std::string> tokens_a = extract_tokens(a.title);
    std::set<std::string> tokens_b = extract_tokens(b.title);

    if (tokens_a.empty() || tokens_b.empty()) return 0.0f;

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

    // A/B 实验：若 session 携带 mmr_lambda 覆盖，优先使用
    float lambda = lambda_;
    if (session.ab_override.mmr_lambda >= 0.f &&
        session.ab_override.mmr_lambda <= 1.f) {
        lambda = session.ab_override.mmr_lambda;
        LOG_INFO("MMRReranker: using A/B lambda={:.2f} (default={:.2f})",
                 lambda, lambda_);
    }

    // A/B 实验：top_k 覆盖
    int top_k = top_k_;
    if (session.ab_override.fine_top_k > 0) {
        top_k = session.ab_override.fine_top_k;
    }

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
    while ((int)selected.size() < top_k && (int)selected.size() < (int)input.size()) {
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

            float mmr = lambda * relevance - (1.0f - lambda) * max_sim;

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
