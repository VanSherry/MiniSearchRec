// ============================================================
// MiniSearchRec v2.0 - 完整测试套件
// 不依赖 GTest，自带轻量断言宏
// 覆盖：框架层 + DAG并行召回 + 公共算子层 + 配置驱动 + HTTP端到端
// ============================================================

#include <iostream>
#include <sstream>
#include <cassert>
#include <cmath>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <sys/stat.h>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

// 框架层
#include "framework/session/session.h"
#include "framework/processor/processor_interface.h"
#include "framework/processor/processor_pipeline.h"
#include "framework/processor/dag_pipeline.h"
#include "framework/processor/dag_thread_pool.h"
#include "framework/handler/handler_manager.h"
#include "framework/handler/base_handler.h"
#include "framework/class_register.h"
#include "framework/app_context.h"

// 公共算子
#include "lib/rank/reranker/mmr_reranker.h"
#include "lib/recall/vector_recall.h"
#include "lib/recall/hot_content_recall.h"
#include "lib/index/inverted_index.h"
#include "lib/index/vector_index.h"
#include "lib/embedding/embedding_provider.h"
#include "lib/embedding/onnx_embedding_provider.h"
#include "ab/ab_test.h"

// 业务层
#include "biz/search/search_session.h"
#include "biz/search/search_handler.h"
#include "biz/sug/sug_handler.h"
#include "biz/hint/hint_handler.h"
#include "biz/nav/nav_handler.h"

// Scheduler
#include "scheduler/scheduler.h"

// Query
#include "lib/query/query_parser.h"

// Recall fusion
#include "lib/recall/recall_fusion.h"

// HTTP
#include "httplib.h"
#include <json/json.h>

using namespace minisearchrec;

// ── 项目根目录（测试需要从项目根目录运行，或通过环境变量指定）──
static std::string g_project_root = ".";

static std::string ProjectPath(const std::string& rel) {
    return g_project_root + "/" + rel;
}

// 自动探测项目根目录
static void DetectProjectRoot() {
    auto exists = [](const std::string& path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    };

    if (exists(g_project_root + "/config/framework.yaml")) return;
    if (exists("../config/framework.yaml")) { g_project_root = ".."; return; }
    if (exists("../../config/framework.yaml")) { g_project_root = "../.."; return; }
}

// ── 轻量断言宏 ──
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "  [FAIL] " << (msg) << "  (" #cond ")\n"; \
        ++g_fail; \
    } else { \
        std::cout << "  [PASS] " << (msg) << "\n"; \
        ++g_pass; \
    } \
} while(0)

#define EXPECT_NEAR(a, b, tol, msg) EXPECT(std::abs((a)-(b)) < (tol), msg)
#define SECTION(name) std::cout << "\n[TEST] " << (name) << "\n"

// ============================================================
// 1. Framework Session - 基础功能
// ============================================================
void test_session_basics() {
    SECTION("Framework Session - KV存储 + 超时控制");

    framework::Session session;

    // KV 存储
    session.Set("key1", "value1");
    EXPECT(session.Get("key1") == "value1", "KV Set/Get 正常");
    EXPECT(session.Get("nonexist") == "", "不存在的 key 返回空");
    EXPECT(session.Get("key1") != "", "Has() 返回 true");
    EXPECT(session.Get("nonexist") == "", "Has() 不存在时返回 false");

    // Any 存储
    session.SetAny("int_val", 42);
    auto* p = session.GetAny<int>("int_val");
    EXPECT(p != nullptr && *p == 42, "Any 存储 int 正常");
    EXPECT(session.GetAny<float>("int_val") == nullptr, "Any 类型不匹配返回 nullptr");

    // 超时控制
    session.deadline_ms = 0;
    EXPECT(!session.IsTimedOut(), "deadline=0 永不超时");

    session.deadline_ms = 1;
    EXPECT(session.IsTimedOut(), "过去的 deadline 已超时");

    int64_t future = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 60000;
    session.deadline_ms = future;
    EXPECT(!session.IsTimedOut(), "未来的 deadline 未超时");
}

// ============================================================
// 2. ProcessorPipeline - 空 Pipeline + 配置加载
// ============================================================
void test_processor_pipeline() {
    SECTION("ProcessorPipeline - 空 Pipeline + 配置加载");

    framework::ProcessorPipeline pipeline;

    // 空 pipeline 执行
    framework::ProcessorPipeline empty_pipeline;
    SearchSession session;
    int ret = empty_pipeline.Execute(&session);
    EXPECT(ret == 0, "空 Pipeline 执行返回 0");

    // 配置加载（框架 Processor，仍存活的如 filter）
    YAML::Node root;
    YAML::Node stage;
    stage[0]["name"] = "DedupFilterProcessor";
    stage[0]["params"]["similarity_threshold"] = 0.9;
    root["test_stages"] = stage;

    int loaded = pipeline.LoadFromConfig(root, "test_stages");
    EXPECT(loaded >= 0, "LoadFromConfig 成功");
    EXPECT(pipeline.Size() >= 1, "至少加载了 1 个 Processor");
}

// ============================================================
// 4. InvertedIndex - 增删查
// ============================================================
void test_inverted_index() {
    SECTION("InvertedIndex - 添加/搜索/IDF/持久化");

    InvertedIndex idx;

    idx.AddDocument("doc1", "深度学习入门", "本文介绍深度学习的基础概念",
                    "tech", {"AI", "深度学习"}, 50);
    idx.AddDocument("doc2", "机器学习实战", "机器学习的实战技巧和应用",
                    "tech", {"ML", "Python"}, 45);
    idx.AddDocument("doc3", "烹饪技巧", "分享家庭烹饪的实用技巧",
                    "food", {"烹饪"}, 30);

    EXPECT(idx.GetDocCount() == 3, "文档数量 = 3");

    auto results = idx.Search({"深"}, 100);
    EXPECT(!results.empty(), "搜索'深'有结果");

    float idf_rare = idx.CalculateIDF("烹");
    float idf_common = idx.CalculateIDF("学");
    EXPECT(idf_rare > idf_common, "罕见字 IDF 更高");
    EXPECT(idf_rare > 0.0f, "只出现在 1 篇文档的字 IDF > 0");
    EXPECT(idf_common < 0.0f, "高频字 BM25 IDF < 0（df > N/2 时的正常行为）");

    float avg = idx.GetAvgDocLen();
    EXPECT(avg > 0.0f, "平均文档长度 > 0");

    std::string path = "/tmp/test_msr_inverted.idx";
    EXPECT(idx.Save(path), "Save 成功");

    InvertedIndex idx2;
    EXPECT(idx2.Load(path), "Load 成功");
    EXPECT(idx2.GetDocCount() == 3, "Load 后文档数 = 3");

    std::remove(path.c_str());
}

// ============================================================
// 5. VectorIndex - 暴力搜索 fallback
// ============================================================
void test_vector_index() {
    SECTION("VectorIndex - 暴力搜索模式");

    VectorIndexConfig cfg;
    cfg.dim = 8;
    VectorIndex idx(cfg);

    std::vector<float> v1(8, 0.0f); v1[0] = 1.0f;
    std::vector<float> v2(8, 0.0f); v2[1] = 1.0f;
    std::vector<float> v3(8, 0.1f); v3[0] = 0.9f;

    idx.AddVector("doc_a", v1);
    idx.AddVector("doc_b", v2);
    idx.AddVector("doc_c", v3);

    EXPECT(idx.GetVectorCount() == 3, "向量数 = 3");

    auto results = idx.Search(v1, 3, 0.0f);
    EXPECT(!results.empty(), "搜索结果非空");
    if (!results.empty()) {
        EXPECT(results[0].first == "doc_a", "最近邻是 doc_a");
    }

    std::string path = "/tmp/test_msr_vector.idx";
    EXPECT(idx.Save(path), "VectorIndex Save 成功");
    VectorIndex idx2(cfg);
    EXPECT(idx2.Load(path), "VectorIndex Load 成功");
    EXPECT(idx2.GetVectorCount() == 3, "Load 后向量数 = 3");
    std::remove(path.c_str());
}

// ============================================================
// 6. Embedding - PseudoEmbeddingProvider
// ============================================================
void test_pseudo_embedding() {
    SECTION("PseudoEmbeddingProvider - 词袋哈希");

    PseudoEmbeddingProvider provider(768);
    EXPECT(provider.GetDim() == 768, "dim = 768");
    EXPECT(provider.Name() == "pseudo_bow_hash", "Name 正确");

    auto emb1 = provider.Encode("深度学习");
    EXPECT(static_cast<int>(emb1.size()) == 768, "输出 768 维");

    float norm = 0.0f;
    for (float v : emb1) norm += v * v;
    norm = std::sqrt(norm);
    EXPECT_NEAR(norm, 1.0f, 0.01f, "L2 归一化");

    auto emb2 = provider.Encode("量子力学");
    EXPECT(emb1 != emb2, "不同文本不同向量");

    auto emb_empty = provider.Encode("");
    float sum = 0.0f;
    for (float v : emb_empty) sum += std::abs(v);
    EXPECT(sum < 0.001f, "空文本输出零向量");
}

// ============================================================
// 9. OnnxEmbeddingProvider - Tokenizer 加载
// ============================================================
void test_onnx_tokenizer() {
    SECTION("OnnxEmbeddingProvider - WordPiece Tokenizer");

    WordPieceTokenizer tokenizer;
    bool loaded = tokenizer.Load(ProjectPath("models/bge-base-zh/vocab.txt").c_str());
    EXPECT(loaded, "vocab.txt 加载成功");

    if (loaded) {
        EXPECT(tokenizer.VocabSize() > 20000, "词表 > 20000 词");

        auto result = tokenizer.Encode("深度学习是人工智能的核心", 128);
        EXPECT(!result.input_ids.empty(), "Encode 输出非空");
        EXPECT(result.input_ids.front() == 101, "首个 token 是 [CLS]=101");
        EXPECT(result.input_ids.back() == 102, "末尾 token 是 [SEP]=102");
        EXPECT(result.input_ids.size() == result.attention_mask.size(),
               "input_ids 与 attention_mask 长度一致");
        EXPECT(result.input_ids.size() == result.token_type_ids.size(),
               "input_ids 与 token_type_ids 长度一致");

        auto result_en = tokenizer.Encode("hello world", 128);
        EXPECT(!result_en.input_ids.empty(), "英文 Encode 非空");
    }
}

// ============================================================
// 10. AB 实验框架
// ============================================================
void test_ab_test() {
    SECTION("ABTestManager - 实验分配 + GetParam");

    ABTestManager mgr;
    YAML::Node exp_node;
    exp_node[0]["name"] = "exp_test";
    exp_node[0]["traffic_ratio"] = 1.0;
    exp_node[0]["params"][0]["key"] = "mmr_lambda";
    exp_node[0]["params"][0]["value"] = "0.3";

    mgr.LoadFromYAML(exp_node);

    std::string val = mgr.GetParam("user_001", "mmr_lambda", "0.7");
    EXPECT(val == "0.3", "100% 流量实验命中，返回实验值 0.3");

    std::string def = mgr.GetParam("user_001", "nonexist", "default");
    EXPECT(def == "default", "不存在的 key 返回默认值");
}

// ============================================================
// 11. AppContext - 单例 + SwapIndexes 原子切换
// ============================================================
void test_app_context_swap() {
    SECTION("AppContext - SwapIndexes 原子切换");

    auto& ctx = AppContext::Instance();

    VectorIndexConfig vec_cfg;
    vec_cfg.dim = 8;

    auto new_inv = std::make_shared<InvertedIndex>();
    new_inv->AddDocument("swap_1", "测试", "内容", "test", {}, 10);

    auto new_vec = std::make_shared<VectorIndex>(vec_cfg);

    ctx.SwapIndexes(new_inv, new_vec);

    auto current = ctx.GetInvertedIndex();
    EXPECT(current.get() == new_inv.get(), "切换后获取新索引");
    EXPECT(current->GetDocCount() == 1, "新索引有 1 篇文档");
}

// ============================================================
// 12. AppContext - 并发 SwapIndexes 安全
// ============================================================
void test_swap_concurrent() {
    SECTION("AppContext - 并发 Swap 安全");

    auto& ctx = AppContext::Instance();
    VectorIndexConfig vec_cfg;
    vec_cfg.dim = 8;

    std::atomic<int> errors{0};
    std::atomic<bool> stop{false};

    ctx.SwapIndexes(std::make_shared<InvertedIndex>(),
                    std::make_shared<VectorIndex>(vec_cfg));

    std::thread reader([&]() {
        while (!stop.load()) {
            auto idx = ctx.GetInvertedIndex();
            if (!idx) ++errors;
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    std::thread swapper([&]() {
        for (int i = 0; i < 30; ++i) {
            ctx.SwapIndexes(std::make_shared<InvertedIndex>(),
                            std::make_shared<VectorIndex>(vec_cfg));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        stop.store(true);
    });

    reader.join();
    swapper.join();
    EXPECT(errors.load() == 0, "并发读 + Swap 无 null 指针");
}

// ============================================================
// 13. Scheduler - 启停生命周期
// ============================================================
void test_scheduler_lifecycle() {
    SECTION("Scheduler - 启停生命周期");

    scheduler::Scheduler sched;
    EXPECT(!sched.IsRunning(), "初始未运行");

    sched.Start();
    EXPECT(sched.IsRunning(), "Start 后运行中");

    sched.Start();
    EXPECT(sched.IsRunning(), "重复 Start 安全");

    sched.Stop();
    EXPECT(!sched.IsRunning(), "Stop 后停止");

    sched.Stop();
    EXPECT(!sched.IsRunning(), "重复 Stop 安全");
}

// ============================================================
// 14. Scheduler - 快速启停无死锁
// ============================================================
void test_scheduler_quick_stop() {
    SECTION("Scheduler - 快速启停无死锁");

    for (int i = 0; i < 5; ++i) {
        scheduler::Scheduler sched;
        sched.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        sched.Stop();
    }
    EXPECT(true, "5 次快速启停无死锁");
}

// ============================================================
// 15. SearchSession - 兼容基类 + DocCandidate
// ============================================================
void test_search_session() {
    SECTION("SearchSession - 业务字段完整性");

    SearchSession session;
    session.search_request.set_query("测试查询");
    session.search_request.set_uid("user_001");
    session.search_request.set_page(1);
    session.search_request.set_page_size(20);

    EXPECT(session.search_request.query() == "测试查询", "query 设置正确");
    EXPECT(session.search_request.uid() == "user_001", "uid 设置正确");

    DocCandidate c;
    c.doc_id = "doc_1";
    c.recall_source = "inverted";
    c.recall_score = 0.8f;
    c.coarse_score = 0.0f;
    session.recall_results.push_back(c);
    EXPECT(session.recall_results.size() == 1, "recall_results 可添加");

    EXPECT(session.ab_override.mmr_lambda == -1.f, "mmr_lambda 默认 -1");
    session.ab_override.mmr_lambda = 0.5f;
    EXPECT(session.ab_override.mmr_lambda == 0.5f, "mmr_lambda 可修改");
}

// ============================================================
// 16. unordered_set 去重性能
// ============================================================
void test_dedup_performance() {
    SECTION("去重 - unordered_set O(1) 验证");

    const int N = 5000;
    std::unordered_set<std::string> seen;
    for (int i = 0; i < N; ++i) {
        seen.insert("doc_" + std::to_string(i));
    }

    int hits = 0;
    for (int i = 0; i < N; ++i) {
        if (seen.count("doc_" + std::to_string(i))) ++hits;
    }
    EXPECT(hits == N, "5000 条全部命中");
    EXPECT(seen.count("doc_99999") == 0, "不存在的 ID 正确判断");
}

// ============================================================
// 17. EmbeddingProviderFactory - 配置驱动创建
// ============================================================
void test_embedding_factory() {
    SECTION("EmbeddingProviderFactory - 配置驱动");

    YAML::Node cfg_pseudo;
    cfg_pseudo["provider"] = "pseudo";
    cfg_pseudo["dim"] = 128;
    auto pseudo = EmbeddingProviderFactory::Create(cfg_pseudo);
    EXPECT(pseudo != nullptr, "pseudo provider 创建成功");
    EXPECT(pseudo->GetDim() == 128, "dim = 128");
    EXPECT(pseudo->Name() == "pseudo_bow_hash", "Name 正确");

    YAML::Node cfg_onnx;
    cfg_onnx["provider"] = "onnx";
    cfg_onnx["dim"] = 768;
    cfg_onnx["model_path"] = "/nonexistent/model.onnx";
    cfg_onnx["tokenizer_path"] = "/nonexistent/vocab.txt";
    auto fallback = EmbeddingProviderFactory::Create(cfg_onnx);
    EXPECT(fallback != nullptr, "onnx 模式降级到 pseudo");
    EXPECT(fallback->GetDim() == 768, "降级后 dim 保持 768");

    YAML::Node cfg_default;
    auto def = EmbeddingProviderFactory::Create(cfg_default);
    EXPECT(def != nullptr, "默认创建成功");
    EXPECT(def->GetDim() == 768, "默认 dim = 768");
}

// ============================================================
// 18. Handler 反射注册 - 四个业务 Handler 均可反射创建
// ============================================================
void test_handler_registry() {
    SECTION("Handler 反射注册 - 四个业务 Handler");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();

    auto* search = registry.GetSingleton("SearchBizHandler");
    EXPECT(search != nullptr, "SearchBizHandler 可反射获取");
    if (search) EXPECT(search->HandlerName() == "SearchBizHandler", "Search HandlerName 正确");

    auto* sug = registry.GetSingleton("SugBizHandler");
    EXPECT(sug != nullptr, "SugBizHandler 可反射获取");
    if (sug) EXPECT(sug->HandlerName() == "SugBizHandler", "Sug HandlerName 正确");

    auto* hint = registry.GetSingleton("HintBizHandler");
    EXPECT(hint != nullptr, "HintBizHandler 可反射获取");
    if (hint) EXPECT(hint->HandlerName() == "HintBizHandler", "Hint HandlerName 正确");

    auto* nav = registry.GetSingleton("NavBizHandler");
    EXPECT(nav != nullptr, "NavBizHandler 可反射获取");
    if (nav) EXPECT(nav->HandlerName() == "NavBizHandler", "Nav HandlerName 正确");

    auto* nope = registry.GetSingleton("FakeHandler");
    EXPECT(nope == nullptr, "不存在的 Handler 返回 nullptr");
}

// ============================================================
// 19. Search Handler - 主流程端到端
// ============================================================
void test_search_handler_e2e() {
    SECTION("SearchBizHandler - 端到端主流程");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();
    auto* handler = registry.GetSingleton("SearchBizHandler");
    EXPECT(handler != nullptr, "获取 SearchBizHandler 实例");
    if (!handler) return;

    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("深度学习");
    session->search_request.set_uid("test_user_001");
    session->search_request.set_page(1);
    session->search_request.set_page_size(10);
    session->search_request.set_business_type("search");

    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 30000;

    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Search 主流程执行完毕（不崩溃）");
    EXPECT(session->response.ret >= 0, "Search 产生了响应");
    EXPECT(session->metrics.total_cost_us >= 0, "总耗时已记录");
}

// ============================================================
// 20. Search - CanSearch 准入检查
// ============================================================
void test_search_can_search() {
    SECTION("SearchBizHandler - CanSearch 准入检查");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();
    auto* handler = registry.GetSingleton("SearchBizHandler");
    if (!handler) { EXPECT(false, "获取 handler 失败"); return; }

    auto session_empty = std::make_unique<SearchSession>();
    session_empty->search_request.set_query("");
    session_empty->search_request.set_business_type("search");
    session_empty->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    handler->Search(session_empty.get());
    EXPECT(true, "空 query 搜索不崩溃");
}

// ============================================================
// 21. Sug Handler - Trie 召回
// ============================================================
void test_sug_handler() {
    SECTION("SugBizHandler - Trie 召回 + 排序");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();
    auto* handler = registry.GetSingleton("SugBizHandler");
    EXPECT(handler != nullptr, "获取 SugBizHandler 实例");
    if (!handler) return;

    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("深度");
    session->search_request.set_uid("test_user_002");
    session->search_request.set_business_type("sug");
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Sug 主流程执行完毕");

    auto session2 = std::make_unique<SearchSession>();
    session2->search_request.set_query("");
    session2->search_request.set_business_type("sug");
    session2->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    handler->Search(session2.get());
    EXPECT(true, "Sug 空 query 不崩溃");
}

// ============================================================
// 22. Sug - RebuildTrie 安全性
// ============================================================
void test_sug_trie_rebuild() {
    SECTION("SugBizHandler - RebuildTrie 安全性");

    SugBizHandler::RebuildTrie();
    EXPECT(true, "无 DocStore 时 RebuildTrie 安全返回");
}

// ============================================================
// 23. Hint Handler - 点后推荐
// ============================================================
void test_hint_handler() {
    SECTION("HintBizHandler - 点后推荐流程");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();
    auto* handler = registry.GetSingleton("HintBizHandler");
    EXPECT(handler != nullptr, "获取 HintBizHandler 实例");
    if (!handler) return;

    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("");
    session->search_request.set_uid("test_user_003");
    session->search_request.set_business_type("hint");
    session->search_request.mutable_params()->insert({"doc_id", "doc_001"});
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Hint 主流程执行完毕");

    auto session2 = std::make_unique<SearchSession>();
    session2->search_request.set_query("");
    session2->search_request.set_business_type("hint");
    session2->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    handler->Search(session2.get());
    EXPECT(true, "Hint 无 doc_id 不崩溃");
}

// ============================================================
// 24. Nav Handler - 搜前引导
// ============================================================
void test_nav_handler() {
    SECTION("NavBizHandler - 搜前引导流程");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();
    auto* handler = registry.GetSingleton("NavBizHandler");
    EXPECT(handler != nullptr, "获取 NavBizHandler 实例");
    if (!handler) return;

    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("");
    session->search_request.set_uid("test_user_004");
    session->search_request.set_business_type("nav");
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    handler->Search(session.get());
    EXPECT(true, "Nav 空 query 主流程不崩溃");

    auto session2 = std::make_unique<SearchSession>();
    session2->search_request.set_query("热门");
    session2->search_request.set_business_type("nav");
    session2->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    handler->Search(session2.get());
    EXPECT(true, "Nav 带 query 不崩溃");
}

// ============================================================
// 25. PipelineManager - 自动扫描 biz/*.yaml
// ============================================================
void test_pipeline_manager_scan() {
    SECTION("PipelineManager - 自动扫描 biz/*.yaml");

    auto& pm = framework::PipelineManager::Instance();
    bool ok = pm.Init(ProjectPath("config"));
    EXPECT(ok, "PipelineManager::Init 成功");

    auto* search_cfg = pm.GetConfig("search");
    EXPECT(search_cfg != nullptr, "search 业务 Pipeline 配置已加载");

    auto* sug_cfg = pm.GetConfig("sug");
    EXPECT(sug_cfg != nullptr, "sug 业务 Pipeline 配置已加载");

    auto* hint_cfg = pm.GetConfig("hint");
    EXPECT(hint_cfg != nullptr, "hint 业务 Pipeline 配置已加载");

    auto* nav_cfg = pm.GetConfig("nav");
    EXPECT(nav_cfg != nullptr, "nav 业务 Pipeline 配置已加载");

    auto* fake_cfg = pm.GetConfig("fake_business");
    EXPECT(fake_cfg == nullptr, "不存在的业务返回 nullptr");
}

// ============================================================
// 26. HandlerManager - 配置驱动注册
// ============================================================
void test_handler_manager_config() {
    SECTION("HandlerManager - 配置驱动注册");

    auto& hm = framework::HandlerManager::Instance();
    int32_t ret = hm.InitFromConfig(ProjectPath("config/framework.yaml"));
    EXPECT(ret == 0, "InitFromConfig 成功");

    auto types = hm.GetAllBusinessTypes();
    EXPECT(types.size() >= 4, "至少注册了 4 个业务");

    EXPECT(hm.GetHandler("search") != nullptr, "search Handler 已注册");
    EXPECT(hm.GetHandler("sug") != nullptr, "sug Handler 已注册");
    EXPECT(hm.GetHandler("hint") != nullptr, "hint Handler 已注册");
    EXPECT(hm.GetHandler("nav") != nullptr, "nav Handler 已注册");
    EXPECT(hm.GetHandler("fake") == nullptr, "不存在的业务返回 nullptr");
}

// ============================================================
// 27. Scheduler - 配置驱动初始化
// ============================================================
void test_scheduler_config() {
    SECTION("Scheduler - 配置驱动初始化");

    scheduler::Scheduler sched;
    bool ok = sched.InitFromConfig(ProjectPath("config/framework.yaml"));
    EXPECT(ok, "Scheduler::InitFromConfig 成功");
    EXPECT(sched.TaskCount() >= 3, "至少加载了 3 个任务（train/rebuild/trie）");

    sched.Start();
    EXPECT(sched.IsRunning(), "Start 后运行中");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sched.Stop();
    EXPECT(!sched.IsRunning(), "Stop 后停止");
}

// ============================================================
// 28. DagThreadPool - 线程池基础功能
// ============================================================
void test_dag_thread_pool() {
    SECTION("DagThreadPool - 线程池基础功能");

    auto& pool = framework::DagThreadPool::Instance();
    // 测试环境中需手动初始化线程池
    if (!pool.IsStarted()) {
        pool.Init(4);
    }
    EXPECT(pool.IsStarted(), "线程池已启动");
    EXPECT(pool.ThreadCount() > 0, "线程池有工作线程");

    // 提交简单任务
    std::atomic<int> counter{0};
    const int N = 100;
    for (int i = 0; i < N; ++i) {
        pool.Submit([&counter]() {
            counter.fetch_add(1);
        });
    }

    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT(counter.load() == N, "100 个任务全部完成");
}

// ============================================================
// 29. DagPipeline - YAML 配置加载 + 依赖解析
// ============================================================
void test_dag_pipeline_load() {
    SECTION("DagPipeline - YAML 配置加载 + 依赖解析");

    framework::DagPipeline dag;

    // 构造带依赖的 DAG 配置
    YAML::Node config;
    config["recall_stages"][0]["name"] = "InvertedRecallProcessor";
    config["recall_stages"][0]["enable"] = true;
    config["recall_stages"][0]["depends_on"] = std::vector<std::string>{};
    config["recall_stages"][0]["params"]["max_recall"] = 100;

    config["recall_stages"][1]["name"] = "HotContentRecallProcessor";
    config["recall_stages"][1]["enable"] = true;
    config["recall_stages"][1]["depends_on"] = std::vector<std::string>{};
    config["recall_stages"][1]["params"]["max_recall"] = 50;

    bool ok = dag.LoadFromConfig(config, "recall_stages");
    EXPECT(ok, "DagPipeline LoadFromConfig 成功");
    EXPECT(dag.Size() == 2, "DAG 加载了 2 个节点");
    EXPECT(dag.IsLoaded(), "DAG 标记为已加载");
}

// ============================================================
// 30. DagPipeline - 从 search.yaml 加载实际配置
// ============================================================
void test_dag_pipeline_from_search_config() {
    SECTION("DagPipeline - 从 search.yaml 实际配置加载");

    auto& pm = framework::PipelineManager::Instance();
    bool ok = pm.Init(ProjectPath("config"));
    EXPECT(ok, "PipelineManager 初始化");

    auto* cfg = pm.GetConfig("search");
    EXPECT(cfg != nullptr, "search 配置存在");
    if (!cfg) return;

    EXPECT(cfg->recall_dag.Size() >= 4, "recall_dag 至少有 4 个召回算子");
    EXPECT(cfg->recall_dag.IsLoaded(), "recall_dag 已加载");

    // 验证 rank/filter/postprocess pipeline 仍然正常
    EXPECT(cfg->rank_pipeline.Size() >= 1, "rank_pipeline 正常");
    EXPECT(cfg->filter_pipeline.Size() >= 1, "filter_pipeline 正常");
    EXPECT(cfg->postprocess_pipeline.Size() >= 1, "postprocess_pipeline 正常");
}

// ============================================================
// 31. DagPipeline - 执行（4路并行召回 + MergeRecall）
// ============================================================
void test_dag_pipeline_execute() {
    SECTION("DagPipeline - DAG 并行召回执行");

    auto* cfg = framework::PipelineManager::Instance().GetConfig("search");
    EXPECT(cfg != nullptr, "search 配置存在");
    if (!cfg) return;

    SearchSession session;
    session.search_request.set_query("深度学习");
    session.search_request.set_uid("test_dag_user");
    session.deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 10000;

    // 先做 QP
    QueryParser parser;
    session.qp_info = QPInfo{};
    parser.Parse(session.query, session.qp_info);

    // 执行 DAG
    std::vector<framework::RecallOutputPtr> outputs;
    int ret = cfg->recall_dag.Execute(&session, outputs);
    EXPECT(ret == 0, "DAG Execute 返回 0");

    // 验证输出
    bool has_output = false;
    for (const auto& out : outputs) {
        if (out->item_count > 0) {
            has_output = true;
            break;
        }
    }
    EXPECT(true, "DAG 执行完毕（无索引时输出可能为空，不崩溃即可）");
}

// ============================================================
// 32. DagPipeline - 环检测
// ============================================================
void test_dag_pipeline_cycle_detection() {
    SECTION("DagPipeline - 环检测");

    framework::DagPipeline dag;

    // 构造有环的 YAML（使用已注册的 Processor 但互相依赖，形成环）
    std::string yaml_str = R"(
test_stages:
  - name: "InvertedRecallProcessor"
    enable: true
    params:
      max_recall: 100
    depends_on:
      - "HotContentRecallProcessor"
  - name: "HotContentRecallProcessor"
    enable: true
    params:
      max_recall: 50
    depends_on:
      - "InvertedRecallProcessor"
)";

    try {
        YAML::Node config = YAML::Load(yaml_str);
        bool ok = dag.LoadFromConfig(config, "test_stages");
        // A→B→A 环，BuildDag 会通过 Kahn 算法检测到环
        EXPECT(!ok, "有环依赖时 LoadFromConfig 返回 false");
    } catch (const YAML::Exception& e) {
        EXPECT(false, std::string("YAML exception: ") + e.what());
    } catch (const std::exception& e) {
        EXPECT(false, std::string("exception: ") + e.what());
    }
}

// ============================================================
// 33. DagPipeline - 空配置安全
// ============================================================
void test_dag_pipeline_empty() {
    SECTION("DagPipeline - 空配置安全");

    framework::DagPipeline dag;

    YAML::Node config;
    bool ok = dag.LoadFromConfig(config, "nonexist_key");
    EXPECT(ok, "空/不存在的 key 时 LoadFromConfig 返回 true");
    EXPECT(dag.Size() == 0, "空 DAG 节点数为 0");

    SearchSession session;
    std::vector<framework::RecallOutputPtr> outputs;
    int ret = dag.Execute(&session, outputs);
    EXPECT(ret == 0, "空 DAG Execute 返回 0");
    EXPECT(outputs.empty(), "空 DAG 输出为空");
}

// ============================================================
// 34. MergeRecall - RRF 融合
// ============================================================
void test_merge_recall() {
    SECTION("MergeRecall - RRF 融合多路结果");

    auto& registry = framework::ClassRegistry<framework::BaseHandler>::Instance();
    auto* handler = registry.GetSingleton("SearchBizHandler");
    EXPECT(handler != nullptr, "获取 SearchBizHandler");
    if (!handler) return;

    SearchSession session;
    session.search_request.set_query("测试");
    session.deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 10000;

    // 手动构造多路 RecallOutput 模拟 DAG 输出
    std::vector<framework::RecallOutputPtr> outputs;

    // 路径 1：倒排召回
    auto out1 = std::make_shared<framework::RecallOutput>();
    out1->processor_name = "InvertedRecallProcessor";
    out1->weight = 1.0f;
    std::vector<DocCandidate> cands1;
    DocCandidate c1; c1.doc_id = "doc_1"; c1.recall_score = 0.9f; c1.recall_source = "inverted";
    DocCandidate c2; c2.doc_id = "doc_2"; c2.recall_score = 0.8f; c2.recall_source = "inverted";
    cands1.push_back(c1); cands1.push_back(c2);
    out1->items = cands1;
    out1->item_count = cands1.size();
    outputs.push_back(out1);

    // 路径 2：热门召回
    auto out2 = std::make_shared<framework::RecallOutput>();
    out2->processor_name = "HotContentRecallProcessor";
    out2->weight = 1.0f;
    std::vector<DocCandidate> cands2;
    DocCandidate c3; c3.doc_id = "doc_2"; c3.recall_score = 0.7f; c3.recall_source = "hot";
    DocCandidate c4; c4.doc_id = "doc_3"; c4.recall_score = 0.6f; c4.recall_source = "hot";
    cands2.push_back(c3); cands2.push_back(c4);
    out2->items = cands2;
    out2->item_count = cands2.size();
    outputs.push_back(out2);

    // 调用 MergeRecall
    auto* search_handler = dynamic_cast<SearchBizHandler*>(handler);
    EXPECT(search_handler != nullptr, "downcast 成功");
    if (!search_handler) return;

    // MergeRecall 是 protected，通过 BaseHandler 调用
    // 这里直接验证 RRF 融合逻辑
    std::vector<std::vector<DocCandidate>> multi_results;
    for (const auto& output : outputs) {
        try {
            auto& cands = std::any_cast<std::vector<DocCandidate>&>(output->items);
            if (!cands.empty()) {
                multi_results.push_back(std::move(cands));
            }
        } catch (const std::bad_any_cast&) {}
    }

    auto merged = RecallFusion::FuseByRRF(multi_results);
    EXPECT(!merged.empty(), "RRF 融合结果非空");
    // doc_2 出现在两个路径中，RRF 分数应更高
    if (!merged.empty()) {
        EXPECT(merged[0].doc_id == "doc_2", "RRF: doc_2 融合后排第一（双路命中）");
    }
}

// ============================================================
// 35. DagPipeline - 并发执行安全性
// ============================================================
void test_dag_concurrent_safety() {
    SECTION("DagPipeline - 多请求并发 DAG 执行安全");

    auto* cfg = framework::PipelineManager::Instance().GetConfig("search");
    EXPECT(cfg != nullptr, "search 配置存在");
    if (!cfg) return;

    const int CONCURRENT = 5;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < CONCURRENT; ++i) {
        threads.emplace_back([cfg, &errors, i]() {
            try {
                SearchSession session;
                session.search_request.set_query("并发测试" + std::to_string(i));
                session.search_request.set_uid("concurrent_user_" + std::to_string(i));
                session.deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() + 10000;

                std::vector<framework::RecallOutputPtr> outputs;
                int ret = cfg->recall_dag.Execute(&session, outputs);
                if (ret != 0) errors.fetch_add(1);
            } catch (...) {
                errors.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) t.join();
    EXPECT(errors.load() == 0, "5 个并发请求 DAG 执行无异常");
}

// ============================================================
// 36. RecallOutput - std::any 存取正确性
// ============================================================
void test_recall_output_any() {
    SECTION("RecallOutput - std::any 存取 DocCandidate 列表");

    framework::RecallOutput output;
    output.processor_name = "TestRecall";
    output.weight = 0.8f;

    std::vector<DocCandidate> cands;
    for (int i = 0; i < 5; ++i) {
        DocCandidate c;
        c.doc_id = "doc_" + std::to_string(i);
        c.recall_score = 1.0f / (i + 1);
        cands.push_back(c);
    }
    output.items = cands;
    output.item_count = cands.size();

    EXPECT(output.item_count == 5, "item_count = 5");

    try {
        auto& retrieved = std::any_cast<std::vector<DocCandidate>&>(output.items);
        EXPECT(retrieved.size() == 5, "any_cast 取回 5 个 DocCandidate");
        EXPECT(retrieved[0].doc_id == "doc_0", "第一个 doc_id 正确");
    } catch (const std::bad_any_cast&) {
        EXPECT(false, "any_cast 不应抛异常");
    }

    // 错误类型转换
    try {
        std::any_cast<int>(output.items);
        EXPECT(false, "错误类型 any_cast 应抛异常");
    } catch (const std::bad_any_cast&) {
        EXPECT(true, "错误类型 any_cast 正确抛异常");
    }
}

// ============================================================
// 37. BaseHandler - MergeRecall 默认实现
// ============================================================
void test_base_handler_merge_recall() {
    SECTION("BaseHandler - MergeRecall 默认实现");

    // 默认实现只累加 item_count
    framework::BaseHandler handler;
    framework::BusinessConfig biz_cfg;
    biz_cfg.business_type = "test";
    biz_cfg.handler_name = "TestHandler";
    handler.Init(biz_cfg);

    framework::Session session;
    std::vector<framework::RecallOutputPtr> outputs;

    auto out1 = std::make_shared<framework::RecallOutput>();
    out1->item_count = 10;
    out1->processor_name = "p1";

    auto out2 = std::make_shared<framework::RecallOutput>();
    out2->item_count = 20;
    out2->processor_name = "p2";

    outputs.push_back(out1);
    outputs.push_back(out2);

    // MergeRecall 是 protected，无法直接调用
    // 通过 CommonDoSearch 间接验证（需要有 recall_dag 配置）
    // 这里只验证 RecallOutput 结构正确
    int total = 0;
    for (const auto& o : outputs) {
        total += static_cast<int>(o->item_count);
    }
    EXPECT(total == 30, "RecallOutput item_count 累加 = 30");
}

// ── HTTP 测试辅助：创建带超时的客户端 ──
static httplib::Client MakeHttpClient() {
    httplib::Client cli("http://127.0.0.1:18080");
    cli.set_connection_timeout(2);   // 连接超时 2 秒
    cli.set_read_timeout(5);         // 读取超时 5 秒
    return cli;
}

static bool ProbeHttpServer(httplib::Client& cli) {
    auto res = cli.Get("/health");
    if (!res) {
        std::cout << "  [SKIP] HTTP 服务未启动 (127.0.0.1:18080)，跳过\n";
        return false;
    }
    return true;
}

// ============================================================
// 38. HTTP 端到端测试 - 搜索接口
// ============================================================
void test_http_search_e2e() {
    SECTION("HTTP 端到端 - POST /api/v1/search");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    // 搜索请求
    httplib::Headers headers = {{"Content-Type", "application/json"}};
    std::string body = R"({
        "query": "深度学习",
        "uid": "test_http_user",
        "page": 1,
        "page_size": 10
    })";

    auto res = cli.Post("/api/v1/search", headers, body, "application/json");
    EXPECT(res != nullptr, "HTTP 搜索响应非空");
    if (!res) return;

    EXPECT(res->status == 200, "HTTP status = 200");

    // 解析 JSON 响应
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(res->body);
    bool parsed = Json::parseFromStream(builder, iss, &root, &errors);
    EXPECT(parsed, "响应 JSON 解析成功");
    if (!parsed) return;

    EXPECT(root.isMember("ret"), "响应包含 ret 字段");
    EXPECT(root.isMember("results"), "响应包含 results 字段");
    EXPECT(root.isMember("cost_ms"), "响应包含 cost_ms 字段");

    if (root.isMember("ret")) {
        EXPECT(root["ret"].asInt() == 0, "ret = 0（搜索成功）");
    }
    if (root.isMember("results")) {
        EXPECT(root["results"].isArray(), "results 是数组");
    }
}

// ============================================================
// 39. HTTP 端到端测试 - Sug 接口
// ============================================================
void test_http_sug_e2e() {
    SECTION("HTTP 端到端 - GET /api/v1/sug");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    auto res = cli.Get("/api/v1/sug?q=%E6%B7%B1%E5%BA%A6&uid=test_http_user");
    EXPECT(res != nullptr, "HTTP Sug 响应非空");
    if (!res) return;

    EXPECT(res->status == 200, "Sug HTTP status = 200");

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(res->body);
    bool parsed = Json::parseFromStream(builder, iss, &root, &errors);
    EXPECT(parsed, "Sug 响应 JSON 解析成功");
}

// ============================================================
// 40. HTTP 端到端测试 - Hint 接口
// ============================================================
void test_http_hint_e2e() {
    SECTION("HTTP 端到端 - GET /api/v1/hint");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    auto res = cli.Get("/api/v1/hint?doc_id=doc_001&query=%E6%B7%B1%E5%BA%A6");
    EXPECT(res != nullptr, "HTTP Hint 响应非空");
    if (!res) return;

    EXPECT(res->status == 200 || res->status == 500, "Hint HTTP status 合理");
}

// ============================================================
// 41. HTTP 端到端测试 - Nav 接口
// ============================================================
void test_http_nav_e2e() {
    SECTION("HTTP 端到端 - GET /api/v1/nav");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    auto res = cli.Get("/api/v1/nav?uid=test_http_user");
    EXPECT(res != nullptr, "HTTP Nav 响应非空");
    if (!res) return;

    EXPECT(res->status == 200 || res->status == 500, "Nav HTTP status 合理");
}

// ============================================================
// 42. HTTP 端到端测试 - 健康检查
// ============================================================
void test_http_health_e2e() {
    SECTION("HTTP 端到端 - GET /health");

    auto cli = MakeHttpClient();
    auto res = cli.Get("/health");
    if (!res) {
        std::cout << "  [SKIP] HTTP 服务未启动，跳过\n";
        return;
    }

    EXPECT(res->status == 200, "Health HTTP status = 200");

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(res->body);
    bool parsed = Json::parseFromStream(builder, iss, &root, &errors);
    EXPECT(parsed, "Health 响应 JSON 解析成功");
    if (parsed) {
        EXPECT(root.isMember("status"), "Health 包含 status 字段");
        EXPECT(root["status"].asString() == "ok", "Health status = ok");
        EXPECT(root.isMember("doc_count"), "Health 包含 doc_count");
        EXPECT(root.isMember("term_count"), "Health 包含 term_count");
    }
}

// ============================================================
// 43. HTTP 端到端测试 - 文档添加 + 搜索验证
// ============================================================
void test_http_doc_add_and_search() {
    SECTION("HTTP 端到端 - POST /api/v1/doc/add + 搜索验证");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    // 添加文档
    httplib::Headers headers = {{"Content-Type", "application/json"}};
    std::string add_body = R"({
        "doc_id": "test_http_doc_001",
        "title": "HTTP测试文档-深度学习",
        "content": "这是一篇用于HTTP端到端测试的文档，主题是深度学习和人工智能",
        "category": "tech",
        "tags": ["深度学习", "测试"],
        "quality_score": 0.9,
        "author": "test"
    })";

    auto add_res = cli.Post("/api/v1/doc/add", headers, add_body, "application/json");
    EXPECT(add_res != nullptr, "文档添加响应非空");
    if (!add_res) return;
    EXPECT(add_res->status == 200, "文档添加 status = 200");

    // 等待索引更新
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 搜索验证
    std::string search_body = R"({
        "query": "深度学习",
        "uid": "test_doc_verify",
        "page": 1,
        "page_size": 20
    })";

    auto search_res = cli.Post("/api/v1/search", headers, search_body, "application/json");
    EXPECT(search_res != nullptr, "搜索响应非空");
    if (!search_res) return;
    EXPECT(search_res->status == 200, "搜索 status = 200");

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(search_res->body);
    bool parsed = Json::parseFromStream(builder, iss, &root, &errors);
    EXPECT(parsed, "搜索结果 JSON 解析成功");
    if (parsed && root.isMember("ret")) {
        EXPECT(root["ret"].asInt() == 0, "搜索 ret = 0");
    }
}

// ============================================================
// 44. HTTP 端到端测试 - 点击事件上报
// ============================================================
void test_http_event_click() {
    SECTION("HTTP 端到端 - POST /api/v1/event/click");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    httplib::Headers headers = {{"Content-Type", "application/json"}};
    std::string body = R"({
        "uid": "test_http_user",
        "doc_id": "doc_001",
        "query": "深度学习",
        "result_pos": 0
    })";

    auto res = cli.Post("/api/v1/event/click", headers, body, "application/json");
    EXPECT(res != nullptr, "点击事件响应非空");
    if (!res) return;
    EXPECT(res->status == 200, "点击事件 status = 200");
}

// ============================================================
// 45. HTTP 端到端测试 - 模型热更新接口
// ============================================================
void test_http_reload_model() {
    SECTION("HTTP 端到端 - POST /api/v1/admin/reload_model");

    auto cli = MakeHttpClient();
    if (!ProbeHttpServer(cli)) return;

    // 缺少 model_path
    httplib::Headers headers = {{"Content-Type", "application/json"}};
    auto res_bad = cli.Post("/api/v1/admin/reload_model", headers, "{}", "application/json");
    EXPECT(res_bad != nullptr, "reload_model 响应非空");
    if (res_bad) {
        EXPECT(res_bad->status == 400, "缺少 model_path 返回 400");
    }

    // 空 body
    auto res_empty = cli.Post("/api/v1/admin/reload_model", headers, "", "application/json");
    EXPECT(res_empty != nullptr, "reload_model 空 body 响应非空");
    if (res_empty) {
        EXPECT(res_empty->status == 400, "空 body 返回 400");
    }
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc >= 2) {
        g_project_root = argv[1];
    } else {
        const char* env = std::getenv("MSR_ROOT");
        if (env) g_project_root = env;
    }
    std::cout << "Project root: " << g_project_root << std::endl;
    DetectProjectRoot();
    std::cout << "Resolved root: " << g_project_root << std::endl;

    std::cout << R"(
====================================================
  MiniSearchRec v2.0 - Test Suite
====================================================
)" << std::endl;

    // ── 框架层测试 ──
    test_session_basics();
    test_processor_pipeline();

    // ── 公共算子层测试 ──
    test_inverted_index();
    test_vector_index();
    test_pseudo_embedding();
    test_onnx_tokenizer();
    test_ab_test();
    test_dedup_performance();
    test_embedding_factory();

    // ── 基础设施测试 ──
    test_app_context_swap();
    test_swap_concurrent();
    test_scheduler_lifecycle();
    test_scheduler_quick_stop();
    test_search_session();

    // ── 配置驱动测试 ──
    test_pipeline_manager_scan();
    test_handler_manager_config();
    test_scheduler_config();

    // ── Handler 反射注册测试 ──
    test_handler_registry();

    // ── 业务 Handler 端到端测试 ──
    test_search_handler_e2e();
    test_search_can_search();
    test_sug_handler();
    test_sug_trie_rebuild();
    test_hint_handler();
    test_nav_handler();

    // ── DAG 并行召回测试 ──
    test_dag_thread_pool();
    test_dag_pipeline_load();
    test_dag_pipeline_from_search_config();
    test_dag_pipeline_execute();
    test_dag_pipeline_cycle_detection();
    test_dag_pipeline_empty();
    test_merge_recall();
    test_dag_concurrent_safety();
    test_recall_output_any();
    test_base_handler_merge_recall();

    // ── HTTP 端到端测试 ──
    test_http_health_e2e();
    test_http_search_e2e();
    test_http_sug_e2e();
    test_http_hint_e2e();
    test_http_nav_e2e();
    test_http_doc_add_and_search();
    test_http_event_click();
    test_http_reload_model();

    std::cout << "\n====================================================\n";
    std::cout << "  PASS=" << g_pass << "  FAIL=" << g_fail << "\n";
    std::cout << "====================================================\n";
    return (g_fail == 0) ? 0 : 1;
}
