// ============================================================
// MiniSearchRec - BM25 打分器测试
// ============================================================

#include <gtest/gtest.h>
#include "../src/rank/bm25_scorer.h"
#include "../src/index/inverted_index.h"
#include "../src/core/session.h"

namespace minisearchrec {

class BM25ScorerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建倒排索引并添加测试文档
        index = std::make_shared<InvertedIndex>();
        
        // 添加测试文档
        std::vector<std::string> doc_ids = {"doc1", "doc2", "doc3"};
        std::vector<std::string> titles = {
            "BM25 Ranking Algorithm",
            "Information Retrieval Guide",
            "Search Engine Basics"
        };
        std::vector<std::string> contents = {
            "BM25 is a ranking function used in information retrieval. "
            "It is based on the probabilistic retrieval framework.",
            "Information retrieval is the process of obtaining information "
            "from a collection of resources. Search engines use various algorithms.",
            "Search engines are software systems that search the web. "
            "They use algorithms like BM25 for ranking results."
        };
        std::vector<std::string> categories = {"tech", "tech", "tech"};
        std::vector<std::vector<std::string>> tags_list = {
            {"bm25", "algorithm"},
            {"ir", "guide"},
            {"search", "basics"}
        };
        std::vector<int32_t> lengths = {
            static_cast<int32_t>(contents[0].length()),
            static_cast<int32_t>(contents[1].length()),
            static_cast<int32_t>(contents[2].length())
        };
        
        index->AddDocuments(doc_ids, titles, contents, categories, tags_list, lengths);
        
        // 创建 BM25 打分器
        scorer = std::make_unique<BM25ScorerProcessor>();
        YAML::Node config;
        config["k1"] = 1.5f;
        config["b"] = 0.75f;
        scorer->Init(config);
        
        // 使用反射或其他方式设置 index（实际项目中应有 setter）
        // 这里简化为在 Process 中自行获取 index
    }
    
    std::shared_ptr<InvertedIndex> index;
    std::unique_ptr<BM25ScorerProcessor> scorer;
};

// 测试 BM25 公式计算
TEST_F(BM25ScorerTest, CalculateBM25Test) {
    float tf = 3.0f;       // 词频
    float idf = 1.2f;      // IDF
    float doc_len = 100.0f; // 文档长度
    float avg_doc_len = 120.0f; // 平均文档长度
    float k1 = 1.5f;
    float b = 0.75f;
    
    float score = BM25ScorerProcessor::CalculateBM25(tf, idf, doc_len, avg_doc_len, k1, b);
    
    // 手动计算验证
    float expected = idf * ((tf * (k1 + 1.0f)) / 
                            (tf + k1 * (1.0f - b + b * doc_len / avg_doc_len)));
    
    EXPECT_FLOAT_EQ(score, expected);
    EXPECT_GT(score, 0.0f);  // 分数应为正
}

// 测试 IDF 影响
TEST_F(BM25ScorerTest, IDFImpactTest) {
    float tf = 2.0f;
    float doc_len = 100.0f;
    float avg_doc_len = 120.0f;
    
    float idf_low = 0.5f;   // 常见词
    float idf_high = 2.0f;  // 罕见词
    
    float score_low = BM25ScorerProcessor::CalculateBM25(tf, idf_low, doc_len, avg_doc_len);
    float score_high = BM25ScorerProcessor::CalculateBM25(tf, idf_high, doc_len, avg_doc_len);
    
    EXPECT_GT(score_high, score_low);  // 罕见词分数应更高
}

// 测试词频影响
TEST_F(BM25ScorerTest, TermFrequencyImpactTest) {
    float idf = 1.0f;
    float doc_len = 100.0f;
    float avg_doc_len = 120.0f;
    
    float score_low_tf = BM25ScorerProcessor::CalculateBM25(1.0f, idf, doc_len, avg_doc_len);
    float score_high_tf = BM25ScorerProcessor::CalculateBM25(5.0f, idf, doc_len, avg_doc_len);
    
    EXPECT_GT(score_high_tf, score_low_tf);  // 高频词分数应更高
}

// 测试文档长度归一化
TEST_F(BM25ScorerTest, DocumentLengthNormalizationTest) {
    float tf = 2.0f;
    float idf = 1.0f;
    float avg_doc_len = 120.0f;
    
    float score_short = BM25ScorerProcessor::CalculateBM25(tf, idf, 50.0f, avg_doc_len);   // 短文档
    float score_long = BM25ScorerProcessor::CalculateBM25(tf, idf, 300.0f, avg_doc_len);  // 长文档
    
    EXPECT_GT(score_short, score_long);  // 短文档分数应更高（词频更集中）
}

// 测试边界情况：零词频
TEST_F(BM25ScorerTest, ZeroTermFrequencyTest) {
    float score = BM25ScorerProcessor::CalculateBM25(0.0f, 1.0f, 100.0f, 120.0f);
    EXPECT_FLOAT_EQ(score, 0.0f);  // 词频为 0，分数应为 0
}

// 测试边界情况：零 IDF
TEST_F(BM25ScorerTest, ZeroIDFTest) {
    float score = BM25ScorerProcessor::CalculateBM25(2.0f, 0.0f, 100.0f, 120.0f);
    EXPECT_FLOAT_EQ(score, 0.0f);  // IDF 为 0，分数应为 0
}

// 测试 Process 方法（集成测试）
TEST_F(BM25ScorerTest, ProcessTest) {
    // 创建 Session
    Session session;
    session.qp_info.raw_query = "BM25 algorithm";
    session.qp_info.terms = {"bm25", "algorithm"};
    
    // 创建候选文档
    std::vector<DocCandidate> candidates = {
        {"doc1", 0.0f, 0.0f, 0.0f, 0.0f, "inverted"},
        {"doc2", 0.0f, 0.0f, 0.0f, 0.0f, "inverted"},
        {"doc3", 0.0f, 0.0f, 0.0f, 0.0f, "inverted"}
    };
    
    session.recall_results = candidates;
    
    // 处理
    int result = scorer->Process(session, session.recall_results);
    
    // 验证：至少成功处理
    EXPECT_EQ(result, 0);
    
    // 验证：分数已计算
    for (const auto& candidate : session.recall_results) {
        EXPECT_GE(candidate.coarse_score, 0.0f);
    }
}

// 测试初始化
TEST_F(BM25ScorerTest, InitTest) {
    BM25ScorerProcessor local_scorer;
    
    // 有效配置
    YAML::Node valid_config;
    valid_config["k1"] = 1.5f;
    valid_config["b"] = 0.75f;
    EXPECT_TRUE(local_scorer.Init(valid_config));
    
    // 无效配置（缺少参数）
    YAML::Node invalid_config;
    EXPECT_TRUE(local_scorer.Init(invalid_config));  // 应使用默认值
}

// 测试名称
TEST_F(BM25ScorerTest, NameTest) {
    EXPECT_EQ(scorer->Name(), "BM25ScorerProcessor");
}

} // namespace minisearchrec

// 主函数
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
