// ============================================================
// MiniSearchRec - 离线索引构建工具
// 用法：./build_index --config <config_dir> --input <json_file>
// ============================================================

#include <iostream>
#include <string>
#include <json/json.h>
#include "index/inverted_index.h"
#include "index/vector_index.h"
#include "index/doc_store.h"
#include "index/index_builder.h"
#include "core/config_manager.h"

using namespace minisearchrec;

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --config <path>    Config directory (default: ./config)\n";
    std::cout << "  --input <path>     Input JSON file (default: ./data/sample_docs.json)\n";
    std::cout << "  --db-path <path>   SQLite DB path (default: ./data/docs.db)\n";
    std::cout << "  --index-dir <path> Index output directory (default: ./index)\n";
}

int main(int argc, char* argv[]) {
    std::string config_dir = "./config";
    std::string input_path = "./data/sample_docs.json";
    std::string db_path = "./data/docs.db";
    std::string index_dir = "./index";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_dir = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--db-path" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--index-dir" && i + 1 < argc) {
            index_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "[build_index] Starting index build...\n";
    std::cout << "  Config dir: " << config_dir << "\n";
    std::cout << "  Input file: " << input_path << "\n";
    std::cout << "  DB path: " << db_path << "\n";
    std::cout << "  Index dir: " << index_dir << "\n";

    // 加载配置
    if (!ConfigManager::Instance().LoadAll(config_dir)) {
        std::cerr << "[build_index] Failed to load config\n";
        return 1;
    }

    // 创建索引和存储
    auto inv_idx = std::make_shared<InvertedIndex>();
    auto doc_store = std::make_shared<DocStore>();

    if (!doc_store->Open(db_path)) {
        std::cerr << "[build_index] Failed to open database: " << db_path << "\n";
        return 1;
    }

    // 构建索引
    IndexBuilder builder;
    builder.SetInvertedIndex(inv_idx);
    builder.SetDocStore(doc_store);

    if (!builder.BuildFromJson(input_path)) {
        std::cerr << "[build_index] Failed to build index from: " << input_path << "\n";
        return 1;
    }

    // 保存索引
    if (!builder.SaveIndexes(index_dir)) {
        std::cerr << "[build_index] Failed to save indexes\n";
        return 1;
    }

    std::cout << "[build_index] Index build complete!\n";
    std::cout << "  Total documents: " << inv_idx->GetDocCount() << "\n";
    std::cout << "  Total terms: " << inv_idx->GetTermCount() << "\n";

    return 0;
}
