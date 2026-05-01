// ============================================================
// MiniSearchRec - 日志系统实现
// 有 spdlog 时使用 spdlog，否则降级到 stdout
// ============================================================

#include "utils/logger.h"
#include <memory>
#include <vector>
#include <iostream>
#include <ctime>

#ifdef HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#endif

namespace minisearchrec {
namespace utils {

#ifdef HAVE_SPDLOG

namespace {
static std::shared_ptr<spdlog::logger> g_logger;

spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return spdlog::level::trace;
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO:  return spdlog::level::info;
        case LogLevel::WARN:  return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        default:              return spdlog::level::info;
    }
}
} // anonymous namespace

bool InitLogger(LogLevel level, const std::string& file_path,
                size_t max_size_mb, size_t max_files) {
    try {
        if (g_logger) {
            spdlog::drop("minisearchrec");
            g_logger.reset();
        }
        std::vector<spdlog::sink_ptr> sinks;
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        sinks.push_back(console_sink);
        if (!file_path.empty()) {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file_path, max_size_mb * 1024 * 1024, max_files);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
            sinks.push_back(file_sink);
        }
        g_logger = std::make_shared<spdlog::logger>(
            "minisearchrec", sinks.begin(), sinks.end());
        g_logger->set_level(ToSpdlogLevel(level));
        g_logger->flush_on(spdlog::level::info);
        spdlog::register_logger(g_logger);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[logger] Init failed: " << e.what() << "\n";
        return false;
    }
}

void ShutdownLogger() {
    if (g_logger) {
        spdlog::drop("minisearchrec");
        g_logger.reset();
    }
    spdlog::shutdown();
}

std::shared_ptr<spdlog::logger> GetLogger() {
    if (!g_logger) InitLogger(LogLevel::INFO, "", 10, 5);
    return g_logger;
}

void SetLogLevel(LogLevel level) {
    if (g_logger) g_logger->set_level(ToSpdlogLevel(level));
}

#else  // 无 spdlog，降级到 stdout

bool InitLogger(LogLevel /*level*/, const std::string& /*file_path*/,
                size_t /*max_size_mb*/, size_t /*max_files*/) {
    std::cout << "[Logger] spdlog not available, using stdout fallback\n";
    return true;
}

void ShutdownLogger() {}

void SetLogLevel(LogLevel /*level*/) {}

#endif  // HAVE_SPDLOG

} // namespace utils
} // namespace minisearchrec
