// =========================================================
// MiniSearchRec - 程序入口
// =========================================================

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include "core/config_manager.h"
#include "core/factory.h"
#include "core/app_context.h"
#include "core/background_scheduler.h"
#include "service/http_server.h"
#include "utils/logger.h"

// =========================================================
// 信号处理：优雅退出
// =========================================================
static std::atomic<bool> g_shutdown_requested{false};
static minisearchrec::HttpServer* g_server = nullptr;

static void SignalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_shutdown_requested.store(true);
        if (g_server) {
            g_server->Stop();
        }
    }
}

void PrintBanner() {
    std::cout << "============================================\n";
    std::cout << "   MiniSearchRec v1.1\n";
    std::cout << "   Mini Search Recommendation Engine\n";
    std::cout << "   https://github.com/VanSherry/MiniSearchRec\n";
    std::cout << "============================================\n";
}

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --config <path>     Config directory path (default: ./config)\n";
    std::cout << "  --build-index       Force rebuild index from data directory\n";
    std::cout << "  --help              Show this help message\n";
}

using namespace minisearchrec;

int main(int argc, char* argv[]) {
    PrintBanner();

    // 注册信号处理
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // 解析命令行参数
    std::string config_dir  = "./config";
    bool force_rebuild      = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_dir = argv[++i];
        } else if (arg == "--build-index") {
            force_rebuild = true;
        }
    }

    // 初始化日志（使用默认配置，加载配置文件后按需更新级别）
    utils::InitLogger(utils::LogLevel::INFO);

    // 加载配置
    LOG_INFO("Loading config from: {}", config_dir);
    if (!ConfigManager::Instance().LoadAll(config_dir)) {
        LOG_ERROR("Failed to load config from: {}", config_dir);
        std::cerr << "[ERROR] Failed to load config from: " << config_dir << "\n";
        return 1;
    }
    LOG_INFO("Config loaded successfully.");

    // 用配置中的日志参数重新初始化 logger（启用文件日志）
    {
        const auto& log_cfg = ConfigManager::Instance().GetGlobalConfig().log;
        auto log_level = utils::LogLevel::INFO;
        if (log_cfg.level == "trace") log_level = utils::LogLevel::TRACE;
        else if (log_cfg.level == "debug") log_level = utils::LogLevel::DEBUG;
        else if (log_cfg.level == "warn") log_level = utils::LogLevel::WARN;
        else if (log_cfg.level == "error") log_level = utils::LogLevel::ERROR;
        utils::InitLogger(log_level, log_cfg.file, log_cfg.max_size_mb, log_cfg.max_files);
        LOG_INFO("Logger re-initialized: level={}, file={}", log_cfg.level, log_cfg.file);
    }

    // 注册所有内置处理器（Factory）
    RegisterBuiltinProcessors();
    LOG_INFO("Builtin processors registered.");

    // 初始化全局应用上下文（加载/构建索引）
    const auto& global_cfg = ConfigManager::Instance().GetGlobalConfig();
    LOG_INFO("Initializing AppContext, data_dir={}, index_dir={}, rebuild={}",
             global_cfg.index.data_dir, global_cfg.index.index_dir, force_rebuild);

    if (!AppContext::Instance().Initialize(
            global_cfg.index.data_dir,
            global_cfg.index.index_dir,
            force_rebuild || global_cfg.index.rebuild_on_start)) {
        LOG_ERROR("AppContext initialization failed!");
        std::cerr << "[ERROR] AppContext initialization failed!\n";
        return 1;
    }
    LOG_INFO("AppContext ready.");

    // 如果只是构建索引，退出
    if (force_rebuild) {
        LOG_INFO("Index rebuild complete. Exiting.");
        std::cout << "[INFO] Index rebuild complete.\n";
        utils::ShutdownLogger();
        return 0;
    }

    // ── 启动后台调度器 ──
    BackgroundScheduler scheduler;
    {
        const auto& bg_cfg = global_cfg.background;

        AutoTrainConfig train_cfg;
        train_cfg.enable = bg_cfg.auto_train.enable;
        train_cfg.interval_hours = bg_cfg.auto_train.interval_hours;
        train_cfg.min_events = bg_cfg.auto_train.min_events;
        train_cfg.check_interval_sec = bg_cfg.auto_train.check_interval_sec;
        train_cfg.train_script = bg_cfg.auto_train.train_script;
        train_cfg.model_output = bg_cfg.auto_train.model_output;
        train_cfg.events_db = bg_cfg.auto_train.events_db;
        train_cfg.docs_db = bg_cfg.auto_train.docs_db;
        train_cfg.train_data_output = bg_cfg.auto_train.train_data_output;
        train_cfg.dump_tool = bg_cfg.auto_train.dump_tool;
        scheduler.SetAutoTrainConfig(train_cfg);

        AutoIndexRebuildConfig rebuild_cfg;
        rebuild_cfg.enable = bg_cfg.auto_index_rebuild.enable;
        rebuild_cfg.interval_hours = bg_cfg.auto_index_rebuild.interval_hours;
        rebuild_cfg.min_doc_changes = bg_cfg.auto_index_rebuild.min_doc_changes;
        rebuild_cfg.check_interval_sec = bg_cfg.auto_index_rebuild.check_interval_sec;
        scheduler.SetAutoIndexRebuildConfig(rebuild_cfg);
    }
    scheduler.Start();

    // ── 启动 HTTP 服务器 ──
    int port        = global_cfg.server.port;
    std::string host = global_cfg.server.host;
    LOG_INFO("Starting HTTP server on {}:{}", host, port);
    std::cout << "[INFO] Server starting on " << host << ":" << port << "\n";

    auto server = std::make_unique<HttpServer>(host, port);
    g_server = server.get();

    if (!server->Initialize()) {
        LOG_ERROR("Failed to initialize HTTP server!");
        scheduler.Stop();
        return 1;
    }

    LOG_INFO("Server ready. Ctrl+C or SIGTERM to stop.");
    std::cout << "[INFO] Server ready. Ctrl+C or SIGTERM to stop.\n";
    server->Run();  // 阻塞直到 Stop() 被调用

    // ── 优雅退出 ──
    LOG_INFO("Shutting down...");
    scheduler.Stop();
    g_server = nullptr;

    LOG_INFO("Shutdown complete.");
    utils::ShutdownLogger();
    return 0;
}
