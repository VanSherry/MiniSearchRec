// =========================================================
// MiniSearchRec - 日志系统
// =========================================================

#ifndef MINISEARCHREC_LOGGER_H_
#define MINISEARCHREC_LOGGER_H_

#include <memory>
#include <string>

// 条件编译：如果定义了 HAVE_SPDLOG，则使用 spdlog
#ifdef HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#endif

namespace minisearchrec {
namespace utils {

enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR };

bool InitLogger(LogLevel level = LogLevel::INFO,
                const std::string& file_path = "",
                size_t max_size_mb = 10,
                size_t max_files = 5);

void ShutdownLogger();

void SetLogLevel(LogLevel level);

#ifdef HAVE_SPDLOG
std::shared_ptr<spdlog::logger> GetLogger();
#endif

} // namespace utils
} // namespace minisearchrec

// =========================================================
// 日志宏
// =========================================================
#ifdef HAVE_SPDLOG
#define LOG_TRACE(...)   SPDLOG_LOGGER_TRACE(::minisearchrec::utils::GetLogger(), __VA_ARGS__)
#define LOG_DEBUG(...)   SPDLOG_LOGGER_DEBUG(::minisearchrec::utils::GetLogger(), __VA_ARGS__)
#define LOG_INFO(...)    SPDLOG_LOGGER_INFO(::minisearchrec::utils::GetLogger(), __VA_ARGS__)
#define LOG_WARN(...)    SPDLOG_LOGGER_WARN(::minisearchrec::utils::GetLogger(), __VA_ARGS__)
#define LOG_ERROR(...)   SPDLOG_LOGGER_ERROR(::minisearchrec::utils::GetLogger(), __VA_ARGS__)
#else
// 无 spdlog 时：宏设为 no-op（不影响编译和运行）
// 如需日志，请安装 spdlog
#define LOG_TRACE(...) ((void)0)
#define LOG_DEBUG(...)   ((void)0)
#define LOG_INFO(...)    ((void)0)
#define LOG_WARN(...)    ((void)0)
#define LOG_ERROR(...)   ((void)0)
#endif

#endif // MINISEARCHREC_LOGGER_H_
