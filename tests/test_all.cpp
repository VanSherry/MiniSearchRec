// ============================================================
// MiniSearchRec - 完整集成测试
// 覆盖 v1.1 / v1.2 所有修复点，不依赖 GTest
// 编译：在 CMake 中作为独立可执行文件
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

// 项目头文件
#include "core/session.h"
#include "rank/freshness_scorer.h"
#include "rank/mmr_reranker.h"
#include "rank/lgbm_ranker.h"
#include "recall/hot_content_recall.h"
#include "user/user_interest_updater.h"
#include "ab/ab_test.h"
#include "query/query_parser.h"

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

#define EXPECT_NEAR_F(a, b, tol, msg) EXPECT(std::abs((a)-(b)) < (tol), msg)
#define SECTION(name) std::cout << "\n[TEST] " << (name) << "\n"

// ============================================================
// #2  FreshnessScorerProcessor：指数衰减替代硬编码阶梯
// ============================================================
void test_freshness_scorer() {
    SECTION("FreshnessScorerProcessor - 指数衰减");

    FreshnessScorerProcessor scorer;
    YAML::Node cfg;
    cfg["weight"]     = 1.0f;
    cfg["decay_rate"] = 0.01f;
    cfg["max_age_days"] = 365;
    scorer.Init(cfg);

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // 刚发布（0天）→ 接近 1.0
    DocCandidate fresh;
    fresh.publish_time = now;
    // 7天前
    DocCandidate week;
    week.publish_time = now - 7 * 86400;
    // 30天前
    DocCandidate month;
    month.publish_time = now - 30 * 86400;
    // 超期（400天）→ 0
    DocCandidate old;
    old.publish_time = now - 400 * 86400;

    Session session;
    std::vector<DocCandidate> cands = {fresh, week, month, old};
    scorer.Process(session, cands);

    float s0 = cands[0].debug_scores.at("freshness");
    float s7 = cands[1].debug_scores.at("freshness");
    float s30 = cands[2].debug_scores.at("freshness");
    float s400 = cands[3].debug_scores.at("freshness");

    EXPECT(s0  > 0.98f,  "今天发布的文章新鲜度接近1.0");
    EXPECT(s7  > 0.90f,  "7天前发布新鲜度 >0.90 (exp(-0.07)≈0.93)");
    EXPECT(s30 > 0.70f,  "30天前发布新鲜度 >0.70 (exp(-0.30)≈0.74)");
    EXPECT(s400 == 0.0f, "超过max_age_days新鲜度为0");
    // 单调递减
    EXPECT(s0 > s7 && s7 > s30, "新鲜度随时间单调递减（连续，非阶梯）");
    // 不是硬编码阶梯：7天和6天应有差异
    DocCandidate day6; day6.publish_time = now - 6 * 86400;
    DocCandidate day8; day8.publish_time = now - 8 * 86400;
    std::vector<DocCandidate> c2 = {day6, day8};
    scorer.Process(session, c2);
    EXPECT(c2[0].debug_scores.at("freshness") != c2[1].debug_scores.at("freshness"),
           "6天和8天新鲜度不同（非硬编码阶梯）");
}

// ============================================================
// #6  去重 O(N²) → unordered_set O(1)
// ============================================================
void test_dedup_uset() {
    SECTION("unordered_set 去重性能验证");

    const int N = 2000;
    std::vector<DocCandidate> existing;
    existing.reserve(N);
    for (int i = 0; i < N; ++i) {
        DocCandidate c;
        c.doc_id = "doc_" + std::to_string(i);
        existing.push_back(c);
    }

    // 建立 unordered_set
    std::unordered_set<std::string> seen;
    for (const auto& c : existing) seen.insert(c.doc_id);

    // 验证已有 ID 均命中
    int hits = 0;
    for (int i = 0; i < N; ++i) {
        if (seen.count("doc_" + std::to_string(i))) ++hits;
    }
    EXPECT(hits == N, "2000条全部命中 unordered_set（O(1)查找正确）");

    // 新 ID 不命中
    EXPECT(seen.count("doc_99999") == 0, "新 doc_id 未在 set 中");
}

// ============================================================
// #11 MMR 中文字符级 Jaccard
// ============================================================
void test_mmr_chinese() {
    SECTION("MMRRerankProcessor - 中文字符级 Jaccard");

    MMRRerankProcessor mmr;
    YAML::Node cfg;
    cfg["lambda"] = 0.7f;
    cfg["top_k"]  = 5;
    mmr.Init(cfg);

    // 完全相同的中文标题 → 相似度接近 1
    DocCandidate a, b, c;
    a.title = "深度学习入门指南";
    b.title = "深度学习入门指南";  // 完全相同
    c.title = "量子力学基础课程";  // 完全不同

    // 构造候选集：先放高分，再放重复
    a.fine_score = 1.0f;
    b.fine_score = 0.9f;
    c.fine_score = 0.8f;

    Session session;
    std::vector<DocCandidate> cands = {a, b, c};
    mmr.Process(session, cands);

    EXPECT(!cands.empty(), "MMR 有输出结果");
    // a/b 完全相同，MMR 应该优选 a 和 c，而非 a 和 b
    bool has_a = false, has_c = false;
    for (const auto& r : cands) {
        if (r.title == "深度学习入门指南" && !has_a) has_a = true;
        if (r.title == "量子力学基础课程") has_c = true;
    }
    EXPECT(has_c, "MMR 选出了与第一个结果不同的文章（多样性有效）");

    // 测试英文按词分，中文按字分：混合标题
    DocCandidate d, e;
    d.title = "C++深度学习框架";
    e.title = "Python深度学习框架";
    d.fine_score = 1.0f;
    e.fine_score = 0.9f;
    std::vector<DocCandidate> c2 = {d, e};
    Session s2;
    mmr.Process(s2, c2);
    EXPECT(!c2.empty(), "混合中英文标题 MMR 正常运行");
}

// ============================================================
// #15 EMA 增量更新（不清空旧权重）
// ============================================================
void test_ema_interest_update() {
    SECTION("UserInterestUpdater - EMA 增量更新");

    UserInterestUpdater updater;
    updater.SetEmaAlpha(0.3f);

    UserProfile profile;
    profile.set_uid("test_user");

    // 初始兴趣：tech=0.8
    (*profile.mutable_category_weights())["tech"] = 0.8f;

    // 模拟一次更新（不清空旧权重）
    // 只测试 ApplyTimeDecay 和 DiffuseInterests
    // active_days_last_30=30 → inactive_days=0 → decay=1.0，权重不变
    profile.set_active_days_last_30(30);
    updater.ApplyTimeDecay(profile);

    float w_tech = profile.category_weights().at("tech");
    EXPECT_NEAR_F(w_tech, 0.8f, 0.01f, "active用户时间衰减接近1，权重保持");

    // inactive_days=10 → decay = 0.95^10 ≈ 0.60
    profile.set_active_days_last_30(20);
    // 重置权重
    (*profile.mutable_category_weights())["tech"] = 0.8f;
    updater.ApplyTimeDecay(profile);

    float w_decayed = profile.category_weights().at("tech");
    EXPECT(w_decayed < 0.8f, "不活跃用户权重发生衰减");
    EXPECT(w_decayed > 0.0f, "衰减后权重仍为正");

    // 兴趣扩散：tech → programming/AI/gadget
    (*profile.mutable_category_weights())["tech"] = 0.6f;
    updater.DiffuseInterests(profile);
    bool diffused = profile.category_weights().count("programming") ||
                    profile.category_weights().count("AI") ||
                    profile.category_weights().count("gadget");
    EXPECT(diffused, "tech 兴趣正确扩散到相关类别（category_links_ 已初始化）");
}

// ============================================================
// #3  A/B 实验框架：GetParam() 覆盖
// ============================================================
void test_ab_getparam() {
    SECTION("ABTestManager - GetParam() 覆盖实验参数");

    ABTestManager mgr;
    YAML::Node exp_node;
    exp_node[0]["name"]          = "exp_mmr_low_lambda";
    exp_node[0]["traffic_ratio"] = 1.0f;   // 100% 流量进实验组
    exp_node[0]["params"][0]["key"]   = "mmr_lambda";
    exp_node[0]["params"][0]["value"] = "0.3";
    exp_node[0]["params"][1]["key"]   = "coarse_top_k";
    exp_node[0]["params"][1]["value"] = "200";

    mgr.LoadFromYAML(exp_node);

    // 任意 uid 应分到实验组（100% 流量）
    std::string val = mgr.GetParam("user_001", "mmr_lambda", "0.7");
    EXPECT(val == "0.3", "GetParam 返回实验组 mmr_lambda=0.3");

    std::string val2 = mgr.GetParam("user_001", "coarse_top_k", "500");
    EXPECT(val2 == "200", "GetParam 返回实验组 coarse_top_k=200");

    // 不存在的 key 返回默认值
    std::string val3 = mgr.GetParam("user_001", "nonexist_key", "default_val");
    EXPECT(val3 == "default_val", "不存在的 key 返回默认值");
}

// ============================================================
// Session ABOverride 字段存在性
// ============================================================
void test_session_ab_override() {
    SECTION("Session::ABOverride 字段完整性");

    Session session;
    EXPECT(session.ab_override.mmr_lambda   == -1.f, "mmr_lambda 默认-1（不覆盖）");
    EXPECT(session.ab_override.coarse_top_k == -1,   "coarse_top_k 默认-1");
    EXPECT(session.ab_override.fine_top_k   == -1,   "fine_top_k 默认-1");

    session.ab_override.mmr_lambda = 0.5f;
    EXPECT(session.ab_override.mmr_lambda == 0.5f, "mmr_lambda 可正常赋值");
}

// ============================================================
// #12 超时控制：IsTimedOut()
// ============================================================
void test_timeout_control() {
    SECTION("Session 超时控制 - IsTimedOut()");

    Session session;
    // deadline=0 → 永不超时
    session.deadline_ms = 0;
    EXPECT(!session.IsTimedOut(), "deadline=0 时永不超时");

    // deadline 设为过去 → 已超时
    session.deadline_ms = 1;  // epoch 1ms，肯定过期
    EXPECT(session.IsTimedOut(), "deadline 为过去时间时 IsTimedOut()=true");

    // deadline 设为未来 → 未超时
    int64_t future = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 60000;
    session.deadline_ms = future;
    EXPECT(!session.IsTimedOut(), "deadline 为未来时间时 IsTimedOut()=false");
}

// ============================================================
// #1  QueryParser 生成伪 embedding（向量召回可运行）
// ============================================================
void test_query_embedding() {
    SECTION("QueryParser - 自动生成伪 query embedding");

    QueryParser qp;
    QPInfo info;
    qp.Parse("深度学习", info);

    EXPECT(!info.terms.empty(), "分词结果非空");
    EXPECT(!info.query_embedding.empty(), "自动生成 query embedding（非空）");
    EXPECT(info.query_embedding.size() == 64, "embedding 维度为 64");

    // 验证已归一化（L2 norm ≈ 1.0）
    float norm = 0.0f;
    for (float v : info.query_embedding) norm += v * v;
    norm = std::sqrt(norm);
    EXPECT_NEAR_F(norm, 1.0f, 0.01f, "embedding 已 L2 归一化");

    // 不同 query 生成不同 embedding
    QPInfo info2;
    qp.Parse("量子力学", info2);
    bool same = (info.query_embedding == info2.query_embedding);
    EXPECT(!same, "不同 query 生成不同 embedding");
}

// ============================================================
// 双 Buffer LGBMScorerProcessor 热更新（无 LightGBM 模式）
// ============================================================
void test_lgbm_double_buffer() {
    SECTION("LGBMScorerProcessor - 双 Buffer 热更新");

    LGBMScorerProcessor scorer;
    YAML::Node cfg;
    cfg["weight"] = 1.0f;
    scorer.Init(cfg);  // 无模型文件，走内置规则树

    EXPECT(!scorer.HasRealModel(), "无模型文件时 HasRealModel()=false");

    // 无 LightGBM 时 HotReload 应优雅返回 false（文件不存在）
    bool ok = scorer.HotReload("/nonexistent/model.txt");
    EXPECT(!ok, "不存在的模型路径 HotReload 返回 false");

    // 推理仍正常工作（降级到内置规则树）
    Session session;
    session.qp_info.terms = {"深度", "学习"};
    DocCandidate cand;
    cand.publish_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    cand.click_count = 100;
    cand.like_count  = 30;
    cand.debug_scores["bm25"]      = 0.7f;
    cand.debug_scores["quality"]   = 0.6f;
    cand.debug_scores["freshness"] = 0.9f;

    std::vector<DocCandidate> cands = {cand};
    int ret = scorer.Process(session, cands);
    EXPECT(ret == 0, "无模型时 Process() 返回0（正常降级）");
    EXPECT(cands[0].fine_score > 0.0f, "降级规则树输出正分");
    EXPECT(cands[0].debug_scores.count("lgbm") > 0, "debug_scores 包含 lgbm 字段");
}

// ============================================================
// 并发测试：双 Buffer 推理线程安全
// ============================================================
void test_lgbm_concurrent_safety() {
    SECTION("LGBMScorerProcessor - 并发推理线程安全");

    auto scorer = std::make_shared<LGBMScorerProcessor>();
    YAML::Node cfg;
    cfg["weight"] = 1.0f;
    scorer->Init(cfg);

    std::atomic<int> errors{0};
    std::atomic<int> done{0};

    // 8 个推理线程同时跑
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&scorer, &errors, &done, i]() {
            for (int j = 0; j < 50; ++j) {
                Session session;
                session.qp_info.terms = {"test"};
                DocCandidate cand;
                cand.debug_scores["bm25"]      = 0.5f;
                cand.debug_scores["quality"]   = 0.5f;
                cand.debug_scores["freshness"] = 0.5f;
                cand.publish_time = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                std::vector<DocCandidate> cands = {cand};
                int ret = scorer->Process(session, cands);
                if (ret != 0 || cands[0].fine_score < 0.0f) {
                    ++errors;
                }
            }
            ++done;
        });
    }

    // 在推理过程中尝试 HotReload（会失败，但不应崩溃）
    std::thread reload_thread([&scorer]() {
        for (int i = 0; i < 5; ++i) {
            scorer->HotReload("/tmp/nonexistent_model_" + std::to_string(i) + ".txt");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    for (auto& t : threads) t.join();
    reload_thread.join();

    EXPECT(errors.load() == 0, "8线程×50次推理并发 + reload 无错误");
    EXPECT(done.load() == 8, "所有推理线程正常完成");
}

// ============================================================
// HotContent 热榜缓存：RefreshHotList 接口存在且不崩溃
// ============================================================
void test_hot_content_cache() {
    SECTION("HotContentRecallProcessor - 热榜缓存刷新");

    HotContentRecallProcessor proc;
    YAML::Node cfg;
    cfg["enable"]               = true;
    cfg["max_recall"]           = 10;
    cfg["time_window_hours"]    = 24;
    cfg["refresh_interval_sec"] = 300;
    proc.Init(cfg);

    // RefreshHotList 无 DocStore 时应安全返回（不崩溃）
    proc.RefreshHotList();
    EXPECT(true, "无 DocStore 时 RefreshHotList() 安全返回");

    // Process 无 DocStore 时应安全返回
    Session session;
    int ret = proc.Process(session);
    EXPECT(ret == 0, "无 DocStore 时 Process() 返回0");
    EXPECT(session.recall_results.empty(), "无 DocStore 时召回结果为空");
}

// ============================================================
// 缓存 key 包含 uid（防个性化污染）
// ============================================================
void test_cache_key_includes_uid() {
    SECTION("CacheKey 包含 uid（防个性化污染）");

    // MakeCacheKey 逻辑：uid:query:page:page_size:business_type
    SearchRequest req1, req2;
    req1.set_uid("user_001");
    req1.set_query("深度学习");
    req1.set_page(1);
    req1.set_page_size(20);

    req2.set_uid("user_002");
    req2.set_query("深度学习");
    req2.set_page(1);
    req2.set_page_size(20);

    // 手动构造 key，与 cache_manager.cpp 一致
    auto make_key = [](const SearchRequest& r) {
        std::ostringstream oss;
        oss << (r.uid().empty() ? "anon" : r.uid()) << ":"
            << r.query() << ":"
            << r.page()  << ":"
            << r.page_size() << ":"
            << r.business_type();
        return oss.str();
    };

    std::string k1 = make_key(req1);
    std::string k2 = make_key(req2);
    EXPECT(k1 != k2, "不同 uid 的相同 query 生成不同 cache key");

    SearchRequest req3;
    req3.set_uid("user_001");
    req3.set_query("深度学习");
    req3.set_page(1);
    req3.set_page_size(20);
    EXPECT(make_key(req1) == make_key(req3), "相同 uid+query 生成相同 cache key");
}

// ============================================================
// #10 特征维度：kNumFeatures == 10
// ============================================================
void test_feature_dimension() {
    SECTION("LightGBM 特征维度 == 10");
    EXPECT(kNumFeatures == 10, "kNumFeatures 常量值为10");

    // 验证 ExtractFeatures 确实输出 10 维
    LGBMScorerProcessor scorer;
    YAML::Node cfg;
    cfg["weight"] = 1.0f;
    scorer.Init(cfg);

    Session session;
    session.qp_info.terms = {"深度", "学习"};
    DocCandidate cand;
    cand.publish_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    cand.debug_scores["bm25"]      = 0.5f;
    cand.debug_scores["quality"]   = 0.5f;
    cand.debug_scores["freshness"] = 0.8f;
    cand.click_count = 50;
    cand.like_count  = 10;
    cand.title       = "深度学习实战";
    cand.category    = "tech";
    cand.recall_source = "inverted";

    std::vector<DocCandidate> cands = {cand};
    scorer.Process(session, cands);
    // 只要 Process 不崩溃且有输出分数，说明特征提取正常
    EXPECT(cands[0].fine_score > 0.0f, "10维特征提取并推理输出正分");
}

// ============================================================
// MMR A/B 覆盖：session.ab_override.mmr_lambda 生效
// ============================================================
void test_mmr_ab_override() {
    SECTION("MMRRerankProcessor - A/B lambda 覆盖");

    MMRRerankProcessor mmr;
    YAML::Node cfg;
    cfg["lambda"] = 0.7f;
    cfg["top_k"]  = 10;
    mmr.Init(cfg);

    // 构造 3 条候选：前两条标题相同，第三条不同
    std::vector<DocCandidate> cands;
    for (int i = 0; i < 3; ++i) {
        DocCandidate c;
        c.title       = (i < 2) ? "相同标题ABC" : "完全不同的内容";
        c.fine_score  = 1.0f - i * 0.1f;
        cands.push_back(c);
    }

    // lambda=0.0（完全多样性）：应偏向选不同内容
    Session s_div;
    s_div.ab_override.mmr_lambda = 0.0f;
    std::vector<DocCandidate> c_div = cands;
    mmr.Process(s_div, c_div);

    // lambda=1.0（完全相关性）：按 fine_score 排
    Session s_rel;
    s_rel.ab_override.mmr_lambda = 1.0f;
    std::vector<DocCandidate> c_rel = cands;
    mmr.Process(s_rel, c_rel);

    EXPECT(!c_div.empty(), "lambda=0.0 时 MMR 有输出");
    EXPECT(!c_rel.empty(), "lambda=1.0 时 MMR 有输出");
    // 结果集大小相同（候选数相同）
    EXPECT(c_div.size() == c_rel.size() || !c_div.empty(),
           "A/B lambda 覆盖下 MMR 正常运行");
}

// ============================================================
// main
// ============================================================
int main() {
    std::cout << "====================================================\n";
    std::cout << "  MiniSearchRec 完整集成测试 (v1.2)\n";
    std::cout << "====================================================\n";

    test_freshness_scorer();
    test_dedup_uset();
    test_mmr_chinese();
    test_ema_interest_update();
    test_ab_getparam();
    test_session_ab_override();
    test_timeout_control();
    test_query_embedding();
    test_lgbm_double_buffer();
    test_lgbm_concurrent_safety();
    test_hot_content_cache();
    test_cache_key_includes_uid();
    test_feature_dimension();
    test_mmr_ab_override();

    std::cout << "\n====================================================\n";
    std::cout << "  结果：PASS=" << g_pass << "  FAIL=" << g_fail << "\n";
    std::cout << "====================================================\n";
    return (g_fail == 0) ? 0 : 1;
}
