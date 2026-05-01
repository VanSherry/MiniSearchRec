// ============================================================
// MiniSearchRec - 离线训练样本导出工具
//
// 用法：
//   ./dump_train_data [options]
//
// 选项：
//   --events-db  <path>  事件数据库路径（default: ./data/events.db）
//   --docs-db    <path>  文档数据库路径（default: ./data/docs.db）
//   --output     <path>  输出 LTR 文件路径（default: ./data/train.txt）
//   --min-ts     <ts>    只导出 >= 此时间戳的事件（default: 0，导出全部）
//   --cold-start         生成冷启动合成样本（无真实数据时启用）
//   --help
//
// 输出格式（LightGBM LambdaRank/Pairwise）：
//   label qid:<query_hash> 1:<feat1> 2:<feat2> ... # uid doc_id
//
// label 映射：
//   click   → 2（强正样本）
//   like    → 3（强正样本）
//   share   → 2
//   dwell   → 1（弱正样本，停留 >= 15s）
//   dismiss → 0（负样本）
//   曝光未点击（冷启动推断）→ 0
// ============================================================

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <sqlite3.h>

// ============================================================
// 特征维度（必须与 lgbm_ranker.h kNumFeatures 保持一致）
// ============================================================
static constexpr int NUM_FEATURES = 6;

// ============================================================
// 工具函数
// ============================================================
static void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --events-db  <path>  default: ./data/events.db\n"
              << "  --docs-db    <path>  default: ./data/docs.db\n"
              << "  --output     <path>  default: ./data/train.txt\n"
              << "  --min-ts     <ts>    only export events >= ts\n"
              << "  --cold-start         generate synthetic samples\n"
              << "  --help\n";
}

// 将字符串哈希为正整数（用作 qid）
static uint32_t HashQuery(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

// 特征归一化
static float NormTanh(float x, float scale = 1.0f) {
    return std::tanh(x / scale);
}

// ============================================================
// 从事件库读取样本
// ============================================================
struct EventRow {
    std::string uid;
    std::string doc_id;
    std::string event_type;
    std::string query;
    int result_pos       = -1;
    int duration_ms      = 0;
    float bm25_score     = 0.0f;
    float quality_score  = 0.0f;
    float freshness_score= 0.0f;
    float coarse_score   = 0.0f;
    float fine_score     = 0.0f;
    int64_t click_count  = 0;
    int64_t like_count   = 0;
    float doc_quality    = 0.0f;
    int64_t ts           = 0;
};

static std::vector<EventRow> LoadEvents(const std::string& db_path,
                                         int64_t min_ts) {
    std::vector<EventRow> rows;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db,
                        SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "[dump] Cannot open events db: " << db_path << "\n";
        return rows;
    }

    const char* sql = R"(
        SELECT uid, doc_id, event_type, query, result_pos, duration_ms,
               bm25_score, quality_score, freshness_score,
               coarse_score, fine_score,
               click_count, like_count, doc_quality, ts
        FROM search_events
        WHERE ts >= ?
        ORDER BY query, ts
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[dump] Prepare failed: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return rows;
    }
    sqlite3_bind_int64(stmt, 1, min_ts);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRow r;
        auto col_text = [&](int c) -> std::string {
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
            return t ? t : "";
        };
        r.uid           = col_text(0);
        r.doc_id        = col_text(1);
        r.event_type    = col_text(2);
        r.query         = col_text(3);
        r.result_pos    = sqlite3_column_int(stmt, 4);
        r.duration_ms   = sqlite3_column_int(stmt, 5);
        r.bm25_score    = static_cast<float>(sqlite3_column_double(stmt, 6));
        r.quality_score = static_cast<float>(sqlite3_column_double(stmt, 7));
        r.freshness_score=static_cast<float>(sqlite3_column_double(stmt, 8));
        r.coarse_score  = static_cast<float>(sqlite3_column_double(stmt, 9));
        r.fine_score    = static_cast<float>(sqlite3_column_double(stmt, 10));
        r.click_count   = sqlite3_column_int64(stmt, 11);
        r.like_count    = sqlite3_column_int64(stmt, 12);
        r.doc_quality   = static_cast<float>(sqlite3_column_double(stmt, 13));
        r.ts            = sqlite3_column_int64(stmt, 14);
        rows.push_back(r);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::cout << "[dump] Loaded " << rows.size() << " events from " << db_path << "\n";
    return rows;
}

// ============================================================
// 计算 label
// ============================================================
static int CalcLabel(const EventRow& r) {
    if (r.event_type == "like")                              return 3;
    if (r.event_type == "share")                             return 2;
    if (r.event_type == "click")                             return 2;
    if (r.event_type == "dwell" && r.duration_ms >= 15000)  return 1;
    if (r.event_type == "dismiss")                           return 0;
    return 1;  // dwell < 15s 视为弱正
}

// ============================================================
// 提取特征向量（与 lgbm_ranker.cpp ExtractFeatures 对齐）
// ============================================================
static std::vector<float> ExtractFeatures(const EventRow& r,
                                           int query_term_count) {
    std::vector<float> feat(NUM_FEATURES, 0.0f);
    feat[0] = NormTanh(static_cast<float>(query_term_count), 5.0f);
    feat[1] = r.bm25_score;
    feat[2] = r.quality_score;
    feat[3] = r.freshness_score;
    feat[4] = NormTanh(std::log1pf(static_cast<float>(r.click_count)), 5.0f);
    feat[5] = NormTanh(std::log1pf(static_cast<float>(r.like_count)), 5.0f);
    return feat;
}

// ============================================================
// 冷启动：基于 sample_docs.json 合成样本
// 用文档自身特征构造正负样本对（高质量=正，低质量=负）
// ============================================================
static std::vector<EventRow> GenerateColdStartSamples(
    const std::string& docs_db_path)
{
    std::vector<EventRow> samples;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(docs_db_path.c_str(), &db,
                        SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "[dump] Cannot open docs db for cold start: "
                  << docs_db_path << "\n";
        return samples;
    }

    const char* sql = R"(
        SELECT doc_id, quality_score, click_count, like_count,
               content_length
        FROM docs
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return samples;
    }

    // 每个文档用多个合成 query 生成样本
    std::vector<std::string> queries = {
        "深度学习", "机器学习", "C++编程", "搜索引擎",
        "Protocol Buffer", "神经网络", "人工智能", "算法"
    };

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto doc_id       = std::string(reinterpret_cast<const char*>(
                                sqlite3_column_text(stmt, 0)));
        float quality     = static_cast<float>(sqlite3_column_double(stmt, 1));
        int64_t clicks    = sqlite3_column_int64(stmt, 2);
        int64_t likes     = sqlite3_column_int64(stmt, 3);
        int content_len   = sqlite3_column_int(stmt, 4);

        // 为每个 query 生成一条样本
        for (const auto& q : queries) {
            EventRow r;
            r.uid           = "cold_start";
            r.doc_id        = doc_id;
            r.query         = q;
            r.doc_quality   = quality;
            r.click_count   = clicks;
            r.like_count    = likes;

            // 合成特征：基于文档自身质量模拟 BM25/quality/freshness
            // 高质量文档给高分，模拟真实相关性
            r.bm25_score     = quality * 0.8f;
            r.quality_score  = quality;
            r.freshness_score= 0.5f;  // 冷启动默认中等时效

            // 合成 label：质量高的文档为正样本
            if (quality >= 0.9f || clicks >= 1000) {
                r.event_type = "click";   // label=2
            } else if (quality >= 0.7f || clicks >= 500) {
                r.event_type = "dwell";   // label=1
                r.duration_ms = 20000;
            } else {
                r.event_type = "dismiss"; // label=0
            }

            // 额外生成一个负样本（打乱 BM25 得低分）
            EventRow neg = r;
            neg.bm25_score    = quality * 0.1f;
            neg.quality_score = quality * 0.3f;
            neg.event_type    = "dismiss";

            samples.push_back(r);
            samples.push_back(neg);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::cout << "[dump] Generated " << samples.size()
              << " cold-start samples from " << docs_db_path << "\n";
    return samples;
}

// ============================================================
// 输出 LTR 格式文件
// ============================================================
static bool WriteLtrFile(const std::string& output_path,
                          const std::vector<EventRow>& rows,
                          bool cold_start) {
    std::ofstream ofs(output_path);
    if (!ofs) {
        std::cerr << "[dump] Cannot open output file: " << output_path << "\n";
        return false;
    }

    ofs << "# MiniSearchRec LTR Training Data\n"
        << "# Format: label qid:<hash> 1:f1 2:f2 ... # uid doc_id\n"
        << "# Features:\n"
        << "#   1: query_len (tanh normalized)\n"
        << "#   2: bm25_score\n"
        << "#   3: quality_score\n"
        << "#   4: freshness_score\n"
        << "#   5: log_click (tanh normalized)\n"
        << "#   6: log_like  (tanh normalized)\n"
        << "# Cold-start: " << (cold_start ? "yes" : "no") << "\n"
        << "# Total samples: " << rows.size() << "\n";

    int written = 0;
    for (const auto& r : rows) {
        // 估算 query 词数（按空格/中文字分割的粗略估计）
        int term_count = 1;
        for (size_t i = 0; i < r.query.size(); ) {
            unsigned char c = r.query[i];
            if (c < 0x80) { ++i; }
            else if (c < 0xE0) { i += 2; }
            else { i += 3; ++term_count; }  // 每个中文字计 1 term
        }

        int label = CalcLabel(r);
        uint32_t qid = HashQuery(r.query);
        auto feat = ExtractFeatures(r, term_count);

        ofs << label << " qid:" << qid;
        for (int i = 0; i < NUM_FEATURES; ++i) {
            ofs << " " << (i + 1) << ":" << feat[i];
        }
        ofs << " # " << r.uid << " " << r.doc_id << "\n";
        ++written;
    }

    ofs.close();
    std::cout << "[dump] Written " << written << " samples to " << output_path << "\n";
    return true;
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
    std::string events_db  = "./data/events.db";
    std::string docs_db    = "./data/docs.db";
    std::string output     = "./data/train.txt";
    int64_t     min_ts     = 0;
    bool        cold_start = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--events-db"  && i+1 < argc) events_db  = argv[++i];
        else if (arg == "--docs-db"    && i+1 < argc) docs_db    = argv[++i];
        else if (arg == "--output"     && i+1 < argc) output     = argv[++i];
        else if (arg == "--min-ts"     && i+1 < argc) min_ts     = std::stoll(argv[++i]);
        else if (arg == "--cold-start")               cold_start = true;
        else if (arg == "--help" || arg == "-h") { PrintUsage(argv[0]); return 0; }
    }

    std::cout << "[dump] Starting sample export...\n"
              << "  events-db:  " << events_db  << "\n"
              << "  docs-db:    " << docs_db    << "\n"
              << "  output:     " << output     << "\n"
              << "  min-ts:     " << min_ts     << "\n"
              << "  cold-start: " << (cold_start ? "yes" : "no") << "\n\n";

    // 加载真实事件
    std::vector<EventRow> all_rows = LoadEvents(events_db, min_ts);

    // 冷启动：若真实数据不足，补充合成样本
    if (cold_start || all_rows.empty()) {
        if (all_rows.empty()) {
            std::cout << "[dump] No real events found, generating cold-start samples...\n";
        } else {
            std::cout << "[dump] Adding cold-start samples to supplement real data...\n";
        }
        auto cold = GenerateColdStartSamples(docs_db);
        all_rows.insert(all_rows.end(), cold.begin(), cold.end());
    }

    if (all_rows.empty()) {
        std::cerr << "[dump] No samples to write. "
                  << "Run with --cold-start if no events exist yet.\n";
        return 1;
    }

    if (!WriteLtrFile(output, all_rows, cold_start)) {
        return 1;
    }

    std::cout << "\n[dump] Done! Next step:\n"
              << "  python3 scripts/train_rank_model.py "
              << "--input " << output
              << " --output ./models/rank_model.txt\n";
    return 0;
}
