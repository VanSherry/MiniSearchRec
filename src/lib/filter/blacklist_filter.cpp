// ===========================================================
// MiniSearchRec - 黑名单过滤器实现（V1 阶段）
// ===========================================================

#include "lib/filter/blacklist_filter.h"
#include <fstream>
#include <iostream>

namespace minisearchrec {

int BlacklistFilterProcessor::Init(const YAML::Node& config) {
    if (config["blacklist_file"]) {
        blacklist_file_ = config["blacklist_file"].as<std::string>("");
        LoadBlacklist(blacklist_file_);
    }
    return 0;
}

bool BlacklistFilterProcessor::LoadBlacklist(const std::string& file_path) {
    std::ifstream ifs(file_path);
    if (!ifs) {
        std::cerr << "[BlacklistFilter] Failed to open: " << file_path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;
        blacklist_ids_.insert(line);
    }

    std::cout << "[BlacklistFilter] Loaded " << blacklist_ids_.size()
              << " entries from " << file_path << "\n";
    return true;
}

bool BlacklistFilterProcessor::ShouldKeep(const Session& session,
                                         const DocCandidate& candidate) {
    // 检查文档 ID 是否在黑名单中
    if (blacklist_ids_.count(candidate.doc_id)) {
        return false;
    }

    // 检查作者是否在黑名单中（如果已加载作者信息）
    if (!candidate.author.empty() && blacklist_ids_.count("author:" + candidate.author)) {
        return false;
    }

    return true;
}

} // namespace minisearchrec

// 自动注册到框架 ProcessorRegistry（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(BlacklistFilterProcessor);
