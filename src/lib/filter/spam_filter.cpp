// ============================================================
// MiniSearchRec - 垃圾内容过滤器实现（V1 阶段）
// ============================================================

#include "lib/filter/spam_filter.h"
#include <algorithm>

namespace minisearchrec {

int SpamFilterProcessor::Init(const YAML::Node& config) {
    if (config["spam_threshold"]) {
        spam_threshold_ = config["spam_threshold"].as<float>(0.8f);
    }
    return 0;
}

bool SpamFilterProcessor::ShouldKeep(const Session& session,
                                     const DocCandidate& candidate) {
    // 简化版：检测重复字符、全大写、过长标题等垃圾特征
    const auto& title = candidate.title;

    if (title.empty()) return false;

    // 检测重复字符（如 "AAAAA"）
    if (title.size() >= 5) {
        char first = title[0];
        int repeat = 0;
        for (char c : title) {
            if (c == first) repeat++;
            else break;
        }
        if (repeat >= 5) return false;
    }

    // 检测全大写英文标题
    int upper_count = 0;
    for (char c : title) {
        if (std::isupper(c)) upper_count++;
    }
    if (title.size() > 5 && upper_count > static_cast<int>(title.size()) * 0.8) {
        return false;  // 全大写，可能是垃圾
    }

    return true;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(SpamFilterProcessor);
