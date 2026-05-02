// ==========================================================
// MiniSearchRec - 配置管理器
// ==========================================================

#ifndef MINISEARCHREC_CONFIG_MANAGER_H
#define MINISEARCHREC_CONFIG_MANAGER_H

#include <string>
#include <yaml-cpp/yaml.h>
#include <mutex>
#include <memory>

namespace minisearchrec {

struct GlobalConfig {
    struct ServerConfig {
        int port = 8080;
        int worker_threads = 4;
        int request_timeout_ms = 200;
        std::string host = "0.0.0.0";
    } server;

    struct IndexConfig {
        std::string data_dir = "./data/";
        std::string index_dir = "./index/";
        bool rebuild_on_start = false;
        int rebuild_batch_size = 1000;
    } index;

    struct LogConfig {
        std::string level = "info";
        std::string file = "./logs/minisearchrec.log";
        int max_size_mb = 100;
        int max_files = 5;
    } log;

    struct CacheConfig {
        bool enable = true;
        int local_capacity = 100;
        int redis_port = 6379;
        std::string redis_host = "127.0.0.1";
        int default_ttl_seconds = 300;
    } cache;

    struct EmbeddingConfig {
        std::string provider = "onnx";     // onnx / pseudo
        int dim = 768;                     // 与 bge-base-zh-v1.5 一致
        std::string model_path;            // onnx 模式：ONNX 模型文件路径
        std::string tokenizer_path;        // onnx 模式：vocab.txt 路径
        int max_seq_len = 512;             // onnx 模式：最大序列长度
    } embedding;
};

class ConfigManager {
public:
    static ConfigManager& Instance() {
        static ConfigManager inst;
        return inst;
    }

    bool LoadAll(const std::string& config_dir);

    bool LoadGlobalConfig(const std::string& path);
    bool LoadRecallConfig(const std::string& path);
    bool LoadRankConfig(const std::string& path);
    bool LoadFilterConfig(const std::string& path);

    const GlobalConfig& GetGlobalConfig() const { return global_config_; }
    const YAML::Node& GetRecallConfig() const { return recall_config_; }
    const YAML::Node& GetRankConfig() const { return rank_config_; }
    const YAML::Node& GetFilterConfig() const { return filter_config_; }

    bool IsLoaded() const { return loaded_; }

    bool Reload();

private:
    ConfigManager() = default;

    // 配置校验
    bool ValidateGlobalConfig();
    bool ValidatePort(int port, const std::string& field_name);
    bool ValidatePositive(int value, const std::string& field_name);
    bool ValidateLogLevel(const std::string& level);

    GlobalConfig global_config_;
    YAML::Node recall_config_;
    YAML::Node rank_config_;
    YAML::Node filter_config_;
    std::string config_dir_;
    bool loaded_ = false;
    mutable std::mutex mutex_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_CONFIG_MANAGER_H
