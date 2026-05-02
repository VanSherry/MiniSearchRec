// =========================================================
// MiniSearchRec - 程序入口
// =========================================================

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include "framework/config/config_manager.h"
#include "framework/app_context.h"
#include "scheduler/scheduler.h"
#include "gateway/http_server.h"
#include "framework/server/server.h"
#include "framework/handler/handler_manager.h"
#include "framework/session/session_factory.h"
#include "framework/processor/processor_pipeline.h"
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
    std::cout << R"(

        __  __ ___  ____
       |  \/  / __||  _ \
       | |\/| \__ \| |_) |
       |_|  |_|___/|_| \_\  v2.0

       M i n i S e a r c h R e c

       github.com/VanSherry/MiniSearchRec

)" << std::endl;
}

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --config <path>     Config directory path (default: ./config)\n";
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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_dir = argv[++i];
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

    // 初始化框架层 Processor Pipeline（从 YAML 配置驱动，替代旧的手动注册）
    if (!framework::PipelineManager::Instance().Init(config_dir)) {
        LOG_ERROR("PipelineManager initialization failed!");
        std::cerr << "[ERROR] PipelineManager initialization failed!\n";
        return 1;
    }
    LOG_INFO("PipelineManager initialized (config-driven processors).");

    // 初始化全局应用上下文（加载/构建索引）
    const auto& global_cfg = ConfigManager::Instance().GetGlobalConfig();
    LOG_INFO("Initializing AppContext, data_dir={}, index_dir={}, rebuild={}",
             global_cfg.index.data_dir, global_cfg.index.index_dir, force_rebuild);

    if (!AppContext::Instance().Initialize(
            global_cfg.index.data_dir,
            global_cfg.index.index_dir,
            global_cfg.index.rebuild_on_start)) {
        LOG_ERROR("AppContext initialization failed!");
        std::cerr << "[ERROR] AppContext initialization failed!\n";
        return 1;
    }
    LOG_INFO("AppContext ready.");

    // 业务模块初始化由框架在 HandlerManager::Init → handler->Init → ExtraInit 中自动完成
    // 新增业务不需要改 main.cpp

    // ── 初始化主流程框架（配置驱动，对标通用搜索框架 Server::Init
    {
        using namespace framework;

        Server::Instance().Init();

        // 从 framework.yaml 配置文件自动注册所有 Handler + Session
        // 新增业务只需改配置文件 + 写 Handler 代码，不碰 main.cpp
        int32_t ret = HandlerManager::Instance().InitFromConfig(config_dir + "/framework.yaml");
        if (ret != 0) {
            LOG_ERROR("HandlerManager::InitFromConfig failed, ret={}", ret);
            std::cerr << "[ERROR] HandlerManager initialization failed!\n";
            return 1;
        }

        LOG_INFO("Framework initialized: {} handlers registered.",
                 HandlerManager::Instance().GetAllBusinessTypes().size());
    }

    // ── 启动后台调度器（配置驱动）──
    scheduler::Scheduler scheduler;
    if (!scheduler.InitFromConfig(config_dir + "/framework.yaml")) {
        LOG_WARN("Scheduler: config load failed, no background tasks");
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
