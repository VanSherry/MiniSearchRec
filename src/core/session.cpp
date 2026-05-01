// ============================================================
// MiniSearchRec - Session 实现
// ============================================================

#include "session.h"
#include <random>
#include <sstream>

namespace minisearchrec {

Session::Session() {
    start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 初始化用户画像（使用 Proto 定义）
    user_profile = std::make_unique<UserProfile>();
}

Session::~Session() = default;  // unique_ptr 自动释放

std::string Session::GenerateTraceId() {
    // 生成格式：MSR-<timestamp>-<random>
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1000, 9999);

    std::ostringstream oss;
    oss << "MSR-" << ts << "-" << dist(rng);
    return oss.str();
}

} // namespace minisearchrec
