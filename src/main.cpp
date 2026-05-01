// =========================================================
// MiniSearchRec - 程序入口
// =========================================================

#include <iostream>
#include <memory>
#include "core/config_manager.h"
#include "core/factory.h"
#include "core/app_context.h"
#include "service/http_server.h"
#include "utils/logger.h"

void PrintBanner() {
    std::cout << "============================================\n";
    std::cout << "   MiniSearchRec v1.0\n";
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

    // 启动 HTTP 服务器
    int port        = global_cfg.server.port;
    std::string host = global_cfg.server.host;
    LOG_INFO("Starting HTTP server on {}:{}", host, port);
    std::cout << "[INFO] Server starting on " << host << ":" << port << "\n";

    auto server = std::make_unique<HttpServer>(host, port);
    if (!server->Initialize()) {
        LOG_ERROR("Failed to initialize HTTP server!");
        return 1;
    }

    LOG_INFO("Server ready. Press Ctrl+C to stop.");
    std::cout << "[INFO] Server ready. Press Ctrl+C to stop.\n";
    server->Run();

    utils::ShutdownLogger();
    return 0;
}
