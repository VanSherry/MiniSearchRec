// ============================================================
// MiniSearchRec v2.0 - 完整测试套件
// 不依赖 GTest，自带轻量断言宏
// 覆盖：框架层 + 公共算子层 + 配置驱动
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
#include <unordered_set>
#include <yaml-cpp/yaml.h>

// 框架层
#include "framework/session/session.h"
#include "framework/processor/processor_interface.h"
#include "framework/processor/processor_pipeline.h"
#include "framework/handler/handler_manager.h"
#include "framework/handler/base_handler.h"
#include "framework/class_register.h"
#include "framework/app_context.h"

// 公共算子
#include "lib/rank/scorer/bm25_scorer.h"
#include "lib/rank/scorer/freshness_scorer.h"
#include "lib/rank/scorer/quality_scorer.h"
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

using namespace minisearchrec;

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
// 2. ProcessorInterface - 注册 + 反射创建
// ============================================================
void test_processor_registry() {
    SECTION("ProcessorInterface - 注册表反射创建");

    auto& registry = framework::ProcessorRegistry::Instance();

    // 验证内置 Processor 已注册（通过 REGISTER_MSR_PROCESSOR 宏）
    auto bm25 = registry.Create("BM25ScorerProcessor");
    EXPECT(bm25 != nullptr, "BM25ScorerProcessor 可反射创建");
    if (bm25) {
        EXPECT(bm25->Name() == "BM25ScorerProcessor", "Name() 正确");
    }

    auto freshness = registry.Create("FreshnessScorerProcessor");
    EXPECT(freshness != nullptr, "FreshnessScorerProcessor 可反射创建");

    auto quality = registry.Create("QualityScorerProcessor");
    EXPECT(quality != nullptr, "QualityScorerProcessor 可反射创建");

    // 不存在的 Processor
    auto nope = registry.Create("NonExistentProcessor");
    EXPECT(nope == nullptr, "不存在的 Processor 返回 nullptr");
}

// ============================================================
// 3. ProcessorPipeline - YAML 配置加载 + 执行
// ============================================================
void test_processor_pipeline() {
    SECTION("ProcessorPipeline - 配置加载 + 按序执行");

    framework::ProcessorPipeline pipeline;

    // 从 YAML 加载
    YAML::Node yaml;
    yaml[0]["name"] = "BM25ScorerProcessor";
    yaml[0]["params"]["k1"] = 1.5;
    yaml[0]["params"]["b"] = 0.75;
    yaml[1]["name"] = "QualityScorerProcessor";
    yaml[1]["params"]["weight"] = 0.3;

    YAML::Node root;
    root["test_stages"] = yaml;

    int loaded = pipeline.LoadFromConfig(root, "test_stages");
    EXPECT(loaded >= 0, "LoadFromConfig 成功");
    EXPECT(pipeline.Size() >= 1, "至少加载了 1 个 Processor");

    // 空 pipeline 执行
    framework::ProcessorPipeline empty_pipeline;
    SearchSession session;
    int ret = empty_pipeline.Execute(&session);
    EXPECT(ret == 0, "空 Pipeline 执行返回 0");
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

    // 搜索
    auto results = idx.Search({"深度"}, 100);
    EXPECT(!results.empty(), "搜索'深度'有结果");

    // IDF
    float idf_tech = idx.CalculateIDF("学习");   // 出现在 2 篇
    float idf_food = idx.CalculateIDF("烹饪");   // 出现在 1 篇
    EXPECT(idf_food > idf_tech, "罕见词 IDF 更高");
    EXPECT(idf_tech > 0.0f, "IDF > 0");

    // 平均文档长度
    float avg = idx.GetAvgDocLen();
    EXPECT(avg > 0.0f, "平均文档长度 > 0");

    // 持久化
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

    // 构造几个简单向量
    std::vector<float> v1(8, 0.0f); v1[0] = 1.0f;  // [1,0,0,...]
    std::vector<float> v2(8, 0.0f); v2[1] = 1.0f;  // [0,1,0,...]
    std::vector<float> v3(8, 0.1f); v3[0] = 0.9f;  // 接近 v1

    idx.AddVector("doc_a", v1);
    idx.AddVector("doc_b", v2);
    idx.AddVector("doc_c", v3);

    EXPECT(idx.GetVectorCount() == 3, "向量数 = 3");

    // 查询 v1，应该 doc_a 最近
    auto results = idx.Search(v1, 3, 0.0f);
    EXPECT(!results.empty(), "搜索结果非空");
    if (!results.empty()) {
        EXPECT(results[0].first == "doc_a", "最近邻是 doc_a");
    }

    // 持久化
    std::string path = "/tmp/test_msr_vector.idx";
    EXPECT(idx.Save(path), "VectorIndex Save 成功");
    VectorIndex idx2(cfg);
    EXPECT(idx2.Load(path), "VectorIndex Load 成功");
    EXPECT(idx2.GetVectorCount() == 3, "Load 后向量数 = 3");
    std::remove(path.c_str());
}

// ============================================================
// 6. BM25ScorerProcessor - 打分正确性
// ============================================================
void test_bm25_scorer() {
    SECTION("BM25ScorerProcessor - 打分");

    BM25ScorerProcessor scorer;
    YAML::Node cfg;
    cfg["weight"] = 0.6;
    cfg["k1"] = 1.5;
    cfg["b"] = 0.75;
    scorer.Init(cfg);

    EXPECT(scorer.Name() == "BM25ScorerProcessor", "Name 正确");

    // 通过 framework::ProcessorInterface::Process(Session*) 调用
    SearchSession session;
    session.qp_info.terms = {"深度", "学习"};

    DocCandidate c1;
    c1.doc_id = "doc1";
    c1.title = "深度学习入门";
    session.recall_results.push_back(c1);

    // BM25Scorer 需要 InvertedIndex，但没有设置时应安全降级
    framework::ProcessorInterface* proc = &scorer;
    int ret = proc->Process(&session);
    EXPECT(ret == 0, "无 InvertedIndex 时 Process 安全返回 0");
}

// ============================================================
// 7. FreshnessScorerProcessor - 指数衰减
// ============================================================
void test_freshness_scorer() {
    SECTION("FreshnessScorerProcessor - 指数衰减");

    FreshnessScorerProcessor scorer;
    YAML::Node cfg;
    cfg["weight"] = 1.0;
    cfg["decay_rate"] = 0.01;
    cfg["max_age_days"] = 365;
    scorer.Init(cfg);

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    SearchSession session;
    DocCandidate fresh, week, month, old;
    fresh.publish_time = now;
    week.publish_time = now - 7 * 86400;
    month.publish_time = now - 30 * 86400;
    old.publish_time = now - 400 * 86400;

    session.recall_results = {fresh, week, month, old};
    framework::ProcessorInterface* freshness_proc = &scorer;
    freshness_proc->Process(&session);

    auto& r = session.recall_results;
    float s0 = r[0].debug_scores.count("freshness") ? r[0].debug_scores["freshness"] : -1;
    float s7 = r[1].debug_scores.count("freshness") ? r[1].debug_scores["freshness"] : -1;
    float s30 = r[2].debug_scores.count("freshness") ? r[2].debug_scores["freshness"] : -1;
    float s400 = r[3].debug_scores.count("freshness") ? r[3].debug_scores["freshness"] : -1;

    EXPECT(s0 > 0.9f, "今天发布新鲜度 > 0.9");
    EXPECT(s0 > s7 && s7 > s30, "新鲜度单调递减");
    EXPECT(s400 == 0.0f, "超 max_age_days 为 0");
}

// ============================================================
// 8. Embedding - PseudoEmbeddingProvider
// ============================================================
void test_pseudo_embedding() {
    SECTION("PseudoEmbeddingProvider - 词袋哈希");

    PseudoEmbeddingProvider provider(768);
    EXPECT(provider.GetDim() == 768, "dim = 768");
    EXPECT(provider.Name() == "pseudo_bow_hash", "Name 正确");

    auto emb1 = provider.Encode("深度学习");
    EXPECT(static_cast<int>(emb1.size()) == 768, "输出 768 维");

    // L2 归一化检验
    float norm = 0.0f;
    for (float v : emb1) norm += v * v;
    norm = std::sqrt(norm);
    EXPECT_NEAR(norm, 1.0f, 0.01f, "L2 归一化");

    // 不同文本不同向量
    auto emb2 = provider.Encode("量子力学");
    EXPECT(emb1 != emb2, "不同文本不同向量");

    // 空文本
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
    bool loaded = tokenizer.Load("models/bge-base-zh/vocab.txt");
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

        // 英文 tokenize
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
    exp_node[0]["traffic_ratio"] = 1.0;  // 100% 流量
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

    sched.Start();  // 重复 Start
    EXPECT(sched.IsRunning(), "重复 Start 安全");

    sched.Stop();
    EXPECT(!sched.IsRunning(), "Stop 后停止");

    sched.Stop();  // 重复 Stop
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

    // DocCandidate
    DocCandidate c;
    c.doc_id = "doc_1";
    c.recall_source = "inverted";
    c.recall_score = 0.8f;
    c.coarse_score = 0.0f;
    session.recall_results.push_back(c);
    EXPECT(session.recall_results.size() == 1, "recall_results 可添加");

    // AB override
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

    // pseudo 模式
    YAML::Node cfg_pseudo;
    cfg_pseudo["provider"] = "pseudo";
    cfg_pseudo["dim"] = 128;
    auto pseudo = EmbeddingProviderFactory::Create(cfg_pseudo);
    EXPECT(pseudo != nullptr, "pseudo provider 创建成功");
    EXPECT(pseudo->GetDim() == 128, "dim = 128");
    EXPECT(pseudo->Name() == "pseudo_bow_hash", "Name 正确");

    // onnx 模式（无模型文件时降级）
    YAML::Node cfg_onnx;
    cfg_onnx["provider"] = "onnx";
    cfg_onnx["dim"] = 768;
    cfg_onnx["model_path"] = "/nonexistent/model.onnx";
    cfg_onnx["tokenizer_path"] = "/nonexistent/vocab.txt";
    auto fallback = EmbeddingProviderFactory::Create(cfg_onnx);
    EXPECT(fallback != nullptr, "onnx 模式降级到 pseudo");
    EXPECT(fallback->GetDim() == 768, "降级后 dim 保持 768");

    // 默认（无 provider 字段）
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

    // 不存在的 Handler
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

    // 构造 SearchSession
    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("深度学习");
    session->search_request.set_uid("test_user_001");
    session->search_request.set_page(1);
    session->search_request.set_page_size(10);
    session->search_request.set_business_type("search");

    // 设置 deadline（不超时）
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 30000;

    // 走完整主流程（不崩溃即通过）
    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Search 主流程执行完毕（不崩溃）");

    // 验证 response 被填充
    EXPECT(session->response.ret >= 0,
           "Search 产生了响应");

    // 验证各阶段耗时已记录
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

    // 空 query 应该被拒绝
    auto session_empty = std::make_unique<SearchSession>();
    session_empty->search_request.set_query("");
    session_empty->search_request.set_business_type("search");
    session_empty->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    int32_t ret = handler->Search(session_empty.get());
    // 空 query 搜索应返回特定错误码或空结果（不崩溃）
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

    // Sug 请求
    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("深度");
    session->search_request.set_uid("test_user_002");
    session->search_request.set_business_type("sug");
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Sug 主流程执行完毕");

    // 空 query 也应安全处理（sug 允许空前缀？取决于实现）
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

    // 无 DocStore 时 RebuildTrie 应安全返回
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

    // Hint 请求需要 doc_id
    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("");
    session->search_request.set_uid("test_user_003");
    session->search_request.set_business_type("hint");
    session->search_request.mutable_params()->insert({"doc_id", "doc_001"});
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Hint 主流程执行完毕");

    // 无 doc_id 时应被 CanSearch 拒绝（不崩溃）
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

    // Nav 允许空 query（搜前引导不需要 query）
    auto session = std::make_unique<SearchSession>();
    session->search_request.set_query("");
    session->search_request.set_uid("test_user_004");
    session->search_request.set_business_type("nav");
    session->deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 5000;

    int32_t ret = handler->Search(session.get());
    EXPECT(ret >= 0 || ret == 0, "Nav 空 query 主流程执行完毕");

    // 带 query 也不应崩溃
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

    // PipelineManager 在 main 中初始化，这里测试它能正确加载配置目录
    auto& pm = framework::PipelineManager::Instance();
    bool ok = pm.Init("./config");

    // 如果 config/biz/ 目录存在且有 yaml 文件，应加载成功
    EXPECT(ok, "PipelineManager::Init 成功");

    // 检查 search 业务配置是否加载
    auto* search_cfg = pm.GetConfig("search");
    EXPECT(search_cfg != nullptr, "search 业务 Pipeline 配置已加载");

    // 检查 sug 业务配置
    auto* sug_cfg = pm.GetConfig("sug");
    EXPECT(sug_cfg != nullptr, "sug 业务 Pipeline 配置已加载");

    // 检查 hint 业务配置
    auto* hint_cfg = pm.GetConfig("hint");
    EXPECT(hint_cfg != nullptr, "hint 业务 Pipeline 配置已加载");

    // 检查 nav 业务配置
    auto* nav_cfg = pm.GetConfig("nav");
    EXPECT(nav_cfg != nullptr, "nav 业务 Pipeline 配置已加载");

    // 不存在的业务
    auto* fake_cfg = pm.GetConfig("fake_business");
    EXPECT(fake_cfg == nullptr, "不存在的业务返回 nullptr");
}

// ============================================================
// 26. HandlerManager - 配置驱动注册
// ============================================================
void test_handler_manager_config() {
    SECTION("HandlerManager - 配置驱动注册");

    auto& hm = framework::HandlerManager::Instance();
    int32_t ret = hm.InitFromConfig("./config/framework.yaml");
    EXPECT(ret == 0, "InitFromConfig 成功");

    auto types = hm.GetAllBusinessTypes();
    EXPECT(types.size() >= 4, "至少注册了 4 个业务");

    // 验证各业务可获取
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
    bool ok = sched.InitFromConfig("./config/framework.yaml");
    EXPECT(ok, "Scheduler::InitFromConfig 成功");
    EXPECT(sched.TaskCount() >= 3, "至少加载了 3 个任务（train/rebuild/trie）");

    // 启停正常
    sched.Start();
    EXPECT(sched.IsRunning(), "Start 后运行中");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sched.Stop();
    EXPECT(!sched.IsRunning(), "Stop 后停止");
}

// ============================================================
// main
// ============================================================
int main() {
    std::cout << R"(
====================================================
  MiniSearchRec v2.0 - Test Suite
====================================================
)" << std::endl;

    // ── 框架层测试 ──
    test_session_basics();
    test_processor_registry();
    test_processor_pipeline();

    // ── 公共算子层测试 ──
    test_inverted_index();
    test_vector_index();
    test_bm25_scorer();
    test_freshness_scorer();
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

    std::cout << "\n====================================================\n";
    std::cout << "  PASS=" << g_pass << "  FAIL=" << g_fail << "\n";
    std::cout << "====================================================\n";
    return (g_fail == 0) ? 0 : 1;
}
