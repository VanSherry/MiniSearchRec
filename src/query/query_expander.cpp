// ============================================================
// MiniSearchRec - 查询扩展实现
// ============================================================

#include "query/query_expander.h"
#include "utils/string_utils.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace minisearchrec {

std::vector<std::string> QueryExpander::Expand(const std::vector<std::string>& terms) const {
    if (terms.empty()) return {};
    
    std::vector<std::string> expanded = terms;
    
    // 依次进行各类扩展
    auto with_synonyms = AddSynonyms(expanded);
    auto with_related = AddRelatedTerms(with_synonyms);
    auto with_abbr = ExpandAbbreviations(with_related);
    auto with_category = AddCategoryTerms(with_abbr);
    
    // 过滤掉与原始查询太远的扩展词
    return FilterExpansion(terms, with_category);
}

void QueryExpander::Process(Session& session) const {
    auto expanded_terms = Expand(session.qp_info.terms);
    session.qp_info.terms = expanded_terms;
}

std::vector<std::string> QueryExpander::AddSynonyms(const std::vector<std::string>& terms) {
    std::vector<std::string> result = terms;
    
    // 内置同义词词典（简化版本）
    static const std::unordered_map<std::string, std::vector<std::string>> builtin_synonyms = {
        {"手机", {"移动电话", "智能手机", "cellphone"}},
        {"电脑", {"计算机", "PC", "computer"}},
        {"人工智能", {"AI", "机器学习", "machine learning"}},
        {"汽车", {"轿车", "车辆", "car"}},
        {"北京", {"Beijing", "京城", "Peking"}},
        {"学习", {"研究", "了解", "study", "learn"}},
        {"好吃", {"美味", "佳肴", "delicious"}},
        {"手机", {"智能手机", "移动电话"}},
        {"电影", {"影片", "movie", "film"}},
        {"游戏", {"电玩", "game", "gaming"}},
    };
    
    for (const auto& term : terms) {
        auto it = builtin_synonyms.find(term);
        if (it != builtin_synonyms.end()) {
            for (const auto& syn : it->second) {
                if (std::find(result.begin(), result.end(), syn) == result.end()) {
                    result.push_back(syn);
                }
            }
        }
    }
    
    return result;
}

std::vector<std::string> QueryExpander::AddRelatedTerms(const std::vector<std::string>& terms) {
    std::vector<std::string> result = terms;
    
    // 内置相关词表（简化版本）
    static const std::unordered_map<std::string, std::vector<std::string>> builtin_related = {
        {"手机", {"手机壳", "手机膜", "充电器", "耳机"}},
        {"电脑", {"键盘", "鼠标", "显示器", "笔记本"}},
        {"人工智能", {"深度学习", "神经网络", "NLP", "CV"}},
        {"美食", {"餐厅", "菜谱", "食材", "烹饪"}},
        {"旅游", {"酒店", "机票", "景点", "攻略"}},
    };
    
    for (const auto& term : terms) {
        auto it = builtin_related.find(term);
        if (it != builtin_related.end()) {
            for (const auto& rel : it->second) {
                if (std::find(result.begin(), result.end(), rel) == result.end()) {
                    result.push_back(rel);
                }
            }
        }
    }
    
    return result;
}

std::vector<std::string> QueryExpander::ExpandAbbreviations(const std::vector<std::string>& terms) {
    std::vector<std::string> result = terms;
    
    // 内置缩写词典
    static const std::unordered_map<std::string, std::string> builtin_abbr = {
        {"AI", "人工智能"},
        {"ML", "机器学习"},
        {"DL", "深度学习"},
        {"NLP", "自然语言处理"},
        {"CV", "计算机视觉"},
        {"UI", "用户界面"},
        {"UX", "用户体验"},
        {"API", "应用程序接口"},
        {"CPU", "中央处理器"},
        {"GPU", "图形处理器"},
    };
    
    for (const auto& term : terms) {
        auto it = builtin_abbr.find(term);
        if (it != builtin_abbr.end()) {
            if (std::find(result.begin(), result.end(), it->second) == result.end()) {
                result.push_back(it->second);
            }
        }
        
        // 反向扩展：全称 -> 缩写
        for (const auto& abbr_pair : builtin_abbr) {
            if (term == abbr_pair.second) {
                if (std::find(result.begin(), result.end(), abbr_pair.first) == result.end()) {
                    result.push_back(abbr_pair.first);
                }
            }
        }
    }
    
    return result;
}

std::vector<std::string> QueryExpander::AddCategoryTerms(const std::vector<std::string>& terms) {
    std::vector<std::string> result = terms;
    
    // 内置类别词表
    static const std::unordered_map<std::string, std::vector<std::string>> builtin_category = {
        {"手机", {"iPhone", "华为", "小米", "Samsung"}},
        {"电脑", {"MacBook", "ThinkPad", "Dell", "HP"}},
        {"汽车", {"Tesla", "比亚迪", "丰田", "BMW"}},
        {"美食", {"川菜", "粤菜", "日料", "西餐"}},
    };
    
    for (const auto& term : terms) {
        auto it = builtin_category.find(term);
        if (it != builtin_category.end()) {
            for (const auto& cat : it->second) {
                if (std::find(result.begin(), result.end(), cat) == result.end()) {
                    result.push_back(cat);
                }
            }
        }
    }
    
    return result;
}

std::vector<std::string> QueryExpander::FilterExpansion(
    const std::vector<std::string>& original,
    const std::vector<std::string>& expanded) {
    
    // 限制扩展词数量：最多扩展为原始词数的 2 倍
    size_t max_terms = original.size() * 2;
    if (max_terms < 5) max_terms = 5;
    
    std::vector<std::string> result = original;
    for (const auto& term : expanded) {
        if (std::find(original.begin(), original.end(), term) == original.end()) {
            result.push_back(term);
            if (result.size() >= max_terms) break;
        }
    }
    
    return result;
}

bool QueryExpander::LoadSynonymDict(const std::string& dict_path) {
    std::ifstream file(dict_path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 格式：词\\t同义词1,同义词2,...
        size_t tab_pos = line.find('\t');
        if (tab_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, tab_pos);
        std::string synonyms_str = line.substr(tab_pos + 1);
        
        std::vector<std::string> synonyms;
        std::stringstream ss(synonyms_str);
        std::string syn;
        while (std::getline(ss, syn, ',')) {
            synonyms.push_back(syn);
        }
        
        synonym_dict_[key] = synonyms;
    }
    
    return true;
}

bool QueryExpander::LoadRelatedTermDict(const std::string& dict_path) {
    std::ifstream file(dict_path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t tab_pos = line.find('\t');
        if (tab_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, tab_pos);
        std::string related_str = line.substr(tab_pos + 1);
        
        std::vector<std::string> related;
        std::stringstream ss(related_str);
        std::string rel;
        while (std::getline(ss, rel, ',')) {
            related.push_back(rel);
        }
        
        related_term_dict_[key] = related;
    }
    
    return true;
}

} // namespace minisearchrec
