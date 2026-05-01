// ==========================================================
// MiniSearchRec - 倒排索引实现
// ==========================================================

#include "index/inverted_index.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cctype>
#include <iostream>
#include <unordered_set>
#include <filesystem>

namespace minisearchrec {

namespace {

// 字段权重配置
constexpr float TITLE_WEIGHT = 3.0f;
constexpr float CONTENT_WEIGHT = 1.0f;
constexpr float TAG_WEIGHT = 2.0f;
constexpr float CATEGORY_WEIGHT = 1.5f;

} // anonymous namespace

// ==========================================================
// 简单分词（降级方案，当 cppjieba 不可用时使用）
// ==========================================================
std::vector<std::string> InvertedIndex::SimpleTokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if (c < 0x80) {
            // ASCII 字符
            if (std::isspace(c) || std::ispunct(c)) {
                if (!current.empty()) {
                    std::transform(current.begin(), current.end(),
                                   current.begin(), ::tolower);
                    tokens.push_back(current);
                    current.clear();
                }
                ++i;
            } else {
                current += static_cast<char>(c);
                ++i;
            }
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            // 2字节 UTF-8（如拉丁扩展）
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
            tokens.push_back(text.substr(i, 2));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            // 3字节 UTF-8（CJK 汉字）
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
            tokens.push_back(text.substr(i, 3));
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            // 4字节 UTF-8（emoji 等）
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
            tokens.push_back(text.substr(i, 4));
            i += 4;
        } else {
            ++i;
        }
    }
    if (!current.empty()) {
        std::transform(current.begin(), current.end(), current.begin(), ::tolower);
        tokens.push_back(current);
    }
    return tokens;
}

// ==========================================================
// 分词方法：优先使用 cppjieba，否则降级为简单分词
// ==========================================================
std::vector<std::string> InvertedIndex::Tokenize(const std::string& text) {
    std::vector<std::string> tokens;

#ifdef HAVE_CPPJIEBA
    if (jieba_) {
        jieba_->CutForSearch(text, tokens);
        for (auto& token : tokens) {
            std::transform(token.begin(), token.end(), token.begin(), ::tolower);
        }
    } else {
        tokens = SimpleTokenize(text);
    }
#else
    tokens = SimpleTokenize(text);
#endif

    // 去重（保持顺序）
    std::vector<std::string> deduplicated;
    std::unordered_set<std::string> seen;
    for (auto& token : tokens) {
        if (seen.insert(token).second) {
            deduplicated.push_back(token);
        }
    }
    return deduplicated;
}

// ==========================================================
// 设置 cppjieba 分词器
// ==========================================================
void InvertedIndex::SetJieba(const std::string& dict_path,
                              const std::string& model_path,
                              const std::string& user_dict_path,
                              const std::string& idf_path) {
#ifdef HAVE_CPPJIEBA
    try {
        jieba_ = std::make_unique<cppjieba::Jieba>(
            dict_path, model_path, user_dict_path, idf_path);
    } catch (const std::exception& e) {
        std::cerr << "[InvertedIndex] Failed to initialize cppjieba: "
                  << e.what() << std::endl;
        jieba_.reset();
    }
#endif
}

void InvertedIndex::AddDocument(const std::string& doc_id,
                                const std::string& title,
                                const std::string& content,
                                const std::string& category,
                                const std::vector<std::string>& tags,
                                int32_t content_length) {
    std::unique_lock<std::shared_mutex> lock(rwlock_);

    uint32_t doc_len = content_length > 0 ? content_length
                                          : static_cast<uint32_t>(content.size());

    // ── 幂等处理：先清除该 doc_id 的旧词条，再从零重建 ──
    // 防止同一文档重复添加导致词频虚增、旧词条残留
    bool is_new_doc = (doc_lengths_.find(doc_id) == doc_lengths_.end());
    if (!is_new_doc) {
        // 从所有倒排链中移除该文档（不加锁版，已在锁内）
        auto terms_it = doc_terms_.find(doc_id);
        if (terms_it != doc_terms_.end()) {
            for (const auto& term : terms_it->second) {
                auto pit = index_.find(term);
                if (pit != index_.end()) {
                    auto& postings = pit->second;
                    postings.erase(
                        std::remove_if(postings.begin(), postings.end(),
                            [&](const PostingNode& n) { return n.doc_id == doc_id; }),
                        postings.end());
                    if (postings.empty()) index_.erase(pit);
                }
            }
            doc_terms_.erase(terms_it);
        }
        // 更新长度表但 total_docs_ 不变（已存在的文档不重计）
        total_doc_len_ -= doc_lengths_[doc_id];
    }
    doc_lengths_[doc_id] = doc_len;
    total_doc_len_ += doc_len;

    // 用 unordered_set 代替 O(N) std::find，提升去重性能
    std::unordered_set<std::string> doc_term_set;

    auto add_tokens = [&](const std::vector<std::string>& tokens, float fw) {
        for (const auto& term : tokens) {
            auto& postings = index_[term];
            bool found = false;
            for (auto& node : postings) {
                if (node.doc_id == doc_id) {
                    node.term_freq++;
                    node.field_weight = std::max(node.field_weight, fw);
                    found = true;
                    break;
                }
            }
            if (!found) {
                PostingNode node;
                node.doc_id = doc_id;
                node.term_freq = 1;
                node.field_weight = fw;
                node.doc_len = doc_len;
                postings.push_back(node);
            }
            doc_term_set.insert(term);
        }
    };

    add_tokens(Tokenize(title),    TITLE_WEIGHT);
    add_tokens(Tokenize(content),  CONTENT_WEIGHT);
    for (const auto& tag : tags) {
        add_tokens(Tokenize(tag),  TAG_WEIGHT);
    }
    if (!category.empty()) {
        add_tokens(Tokenize(category), CATEGORY_WEIGHT);
    }

    doc_terms_[doc_id].assign(doc_term_set.begin(), doc_term_set.end());

    if (is_new_doc) {
        total_docs_++;
    }
    // 增量更新平均文档长度，O(1)
    avg_doc_len_ = doc_lengths_.empty() ? 0.0f
                 : static_cast<float>(total_doc_len_) / static_cast<float>(doc_lengths_.size());
}

std::vector<std::string> InvertedIndex::Search(
    const std::vector<std::string>& terms,
    int max_results
) {
    std::shared_lock<std::shared_mutex> lock(rwlock_);

    std::unordered_map<std::string, int> doc_scores;
    for (const auto& term : terms) {
        // 直接在索引中查找（调用方应已完成分词，这里不再二次分词）
        auto it = index_.find(term);
        if (it != index_.end()) {
            for (const auto& node : it->second) {
                doc_scores[node.doc_id]++;
            }
        }
    }

    std::vector<std::pair<std::string, int>> sorted(
        doc_scores.begin(), doc_scores.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });

    std::vector<std::string> results;
    for (int i = 0; i < std::min(max_results, (int)sorted.size()); ++i) {
        results.push_back(sorted[i].first);
    }
    return results;
}

std::vector<std::string> InvertedIndex::SearchAnd(
    const std::vector<std::string>& terms
) {
    std::shared_lock<std::shared_mutex> lock(rwlock_);

    if (terms.empty()) return {};

    // 直接用传入的 terms 做 AND 查找（调用方已分词）
    auto first_it = index_.find(terms[0]);
    if (first_it == index_.end()) return {};

    std::unordered_set<std::string> result_set;
    for (const auto& node : first_it->second) {
        result_set.insert(node.doc_id);
    }

    for (size_t i = 1; i < terms.size(); ++i) {
        auto it = index_.find(terms[i]);
        if (it == index_.end()) return {};

        std::unordered_set<std::string> new_set;
        for (const auto& node : it->second) {
            if (result_set.count(node.doc_id)) {
                new_set.insert(node.doc_id);
            }
        }
        result_set = std::move(new_set);
        if (result_set.empty()) return {};
    }

    return std::vector<std::string>(result_set.begin(), result_set.end());
}

float InvertedIndex::CalculateIDF(const std::string& term) const {
    std::shared_lock<std::shared_mutex> lock(rwlock_);  // BUG-5 修复：加读锁
    auto it = index_.find(term);
    if (it == index_.end() || total_docs_ == 0) {
        return 0.0f;
    }

    int doc_freq = static_cast<int>(it->second.size());
    if (doc_freq == 0) return 0.0f;
    float numerator = static_cast<float>(total_docs_ - doc_freq) + 0.5f;
    float denominator = static_cast<float>(doc_freq) + 0.5f;
    return std::log(numerator / denominator);
}

std::unordered_map<std::string, PostingNode> InvertedIndex::GetDocPostings(
    const std::string& doc_id
) const {
    std::shared_lock<std::shared_mutex> lock(rwlock_);

    std::unordered_map<std::string, PostingNode> result;
    auto it = doc_terms_.find(doc_id);
    if (it == doc_terms_.end()) return result;

    for (const auto& term : it->second) {
        auto term_it = index_.find(term);
        if (term_it == index_.end()) continue;
        for (const auto& node : term_it->second) {
            if (node.doc_id == doc_id) {
                result[term] = node;
                break;
            }
        }
    }
    return result;
}

// RecalculateAvgDocLen 已改为增量维护，此处保留空实现以兼容 Load 调用
void InvertedIndex::RecalculateAvgDocLen() {
    if (doc_lengths_.empty()) {
        avg_doc_len_ = 0.0f;
        total_doc_len_ = 0;
        return;
    }
    // Load 后重算一次（全量，仅在启动时调用）
    total_doc_len_ = 0;
    for (const auto& [_, len] : doc_lengths_) {
        total_doc_len_ += len;
    }
    avg_doc_len_ = static_cast<float>(total_doc_len_) / static_cast<float>(doc_lengths_.size());
}

void InvertedIndex::Clear() {
    std::unique_lock<std::shared_mutex> lock(rwlock_);
    index_.clear();
    doc_lengths_.clear();
    doc_terms_.clear();
    avg_doc_len_ = 0.0f;
    total_docs_ = 0;
    total_doc_len_ = 0;
}

// ── RemoveDocument：从倒排索引中完整清除一篇文档 ──
// BUG-1/2 修复：Delete/Update 时必须调用此方法清理旧词条
void InvertedIndex::RemoveDocument(const std::string& doc_id) {
    std::unique_lock<std::shared_mutex> lock(rwlock_);

    auto terms_it = doc_terms_.find(doc_id);
    if (terms_it == doc_terms_.end()) return;  // 文档不存在，幂等

    for (const auto& term : terms_it->second) {
        auto pit = index_.find(term);
        if (pit != index_.end()) {
            auto& postings = pit->second;
            postings.erase(
                std::remove_if(postings.begin(), postings.end(),
                    [&](const PostingNode& n) { return n.doc_id == doc_id; }),
                postings.end());
            if (postings.empty()) index_.erase(pit);
        }
    }
    doc_terms_.erase(terms_it);

    auto len_it = doc_lengths_.find(doc_id);
    if (len_it != doc_lengths_.end()) {
        total_doc_len_ -= len_it->second;
        doc_lengths_.erase(len_it);
    }

    if (total_docs_ > 0) total_docs_--;
    avg_doc_len_ = doc_lengths_.empty() ? 0.0f
                 : static_cast<float>(total_doc_len_) / static_cast<float>(doc_lengths_.size());
}

// ==========================================================
// 持久化：以简单文本格式序列化到磁盘
// 格式：
//   TERM <term>
//   NODE <doc_id> <term_freq> <field_weight> <doc_len>
//   ...
//   DOC_LEN <doc_id> <len>
//   ...
//   TOTAL <total_docs>
// ==========================================================
bool InvertedIndex::Save(const std::string& path) const {
    std::shared_lock<std::shared_mutex> lock(rwlock_);

    // BUG-11 修复：先写临时文件，完成后 rename，防止崩溃留下损坏文件
    std::string tmp_path = path + ".tmp";
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs) {
            std::cerr << "[InvertedIndex] Save failed, cannot open: " << tmp_path << "\n";
            return false;
        }

        ofs << "TOTAL " << total_docs_ << "\n";

        for (const auto& [doc_id, len] : doc_lengths_) {
            ofs << "DOC_LEN " << doc_id << " " << len << "\n";
        }

        for (const auto& [doc_id, terms] : doc_terms_) {
            ofs << "DOC_TERMS " << doc_id;
            for (const auto& t : terms) ofs << " " << t;
            ofs << "\n";
        }

        for (const auto& [term, postings] : index_) {
            ofs << "TERM " << term << "\n";
            for (const auto& node : postings) {
                ofs << "NODE " << node.doc_id << " "
                    << node.term_freq << " "
                    << node.field_weight << " "
                    << node.doc_len << "\n";
            }
        }
    }  // ofs 析构，文件关闭

    // 原子替换
    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::cerr << "[InvertedIndex] Save rename failed: " << ec.message() << "\n";
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    std::cout << "[InvertedIndex] Saved to " << path
              << ", terms=" << index_.size()
              << ", docs=" << total_docs_ << "\n";
    return true;
}

bool InvertedIndex::Load(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        // 文件不存在时不报错，视为空索引
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(rwlock_);
    index_.clear();
    doc_lengths_.clear();
    doc_terms_.clear();

    std::string line;
    std::string cur_term;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;

        if (tag == "TOTAL") {
            ls >> total_docs_;
        } else if (tag == "DOC_LEN") {
            std::string doc_id;
            uint32_t len;
            ls >> doc_id >> len;
            doc_lengths_[doc_id] = len;
        } else if (tag == "DOC_TERMS") {
            std::string doc_id;
            ls >> doc_id;
            std::string t;
            while (ls >> t) {
                doc_terms_[doc_id].push_back(t);
            }
        } else if (tag == "TERM") {
            ls >> cur_term;
        } else if (tag == "NODE") {
            PostingNode node;
            ls >> node.doc_id >> node.term_freq >> node.field_weight >> node.doc_len;
            index_[cur_term].push_back(node);
        }
    }

    RecalculateAvgDocLen();
    std::cout << "[InvertedIndex] Loaded from " << path
              << ", terms=" << index_.size()
              << ", docs=" << total_docs_ << "\n";
    return true;
}

} // namespace minisearchrec
