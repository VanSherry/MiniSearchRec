// ============================================================
// MiniSearchRec - 处理管道测试
// ============================================================

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include "../src/core/pipeline.h"
#include "../src/core/session.h"
#include "../src/core/processor.h"
#include "../src/index/inverted_index.h"

namespace minisearchrec {

// 测试用的 Mock 处理器
class MockProcessor : public BaseProcessor {
public:
    MockProcessor(const std::string& name, int* counter)
        : name_(name), counter_(counter) {}
    
    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override {
        if (counter_) (*counter_)++;
        session.debug_info += name_ + "->";
        return 0;  // 成功
    }
    
    std::string Name() const override { return name_; }
    bool Init(const YAML::Node& config) override { return true; }
    
private:
    std::string name_;
    int* counter_;
};

class MockScorerProcessor : public BaseScorerProcessor {
public:
    MockScorerProcessor(const std::string& name, int* counter)
        : name_(name), counter_(counter) {}
    
    int Process(Session& session,
                std::vector<DocCandidate>& candidates) override {
        if (counter_) (*counter_)++;
        for (auto& candidate : candidates) {
            candidate.coarse_score += 1.0f;  // 简单加 1 分
        }
        return 0;
    }
    
    std::string Name() const override { return name_; }
    bool Init(const YAML::Node& config) override { return true; }
    
private:
    std::string name_;
    int* counter_;
};

// ============================================================
// Pipeline 测试
// ============================================================
class PipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        pipeline = std::make_unique<Pipeline>();
    }
    
    std::unique_ptr<Pipeline> pipeline;
};

// 测试空管道
TEST_F(PipelineTest, EmptyPipelineTest) {
    Session session;
    std::vector<DocCandidate> candidates;
    
    int result = pipeline->Run(session, candidates);
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(session.debug_info.empty());
}

// 测试添加处理器
TEST_F(PipelineTest, AddProcessorTest) {
    int counter = 0;
    auto processor = std::make_shared<MockProcessor>("proc1", &counter);
    
    pipeline->AddProcessor(processor);
    EXPECT_EQ(pipeline->GetProcessorCount(), 1);
}

// 测试 processors 执行顺序
TEST_F(PipelineTest, ProcessorOrderTest) {
    Session session;
    std::vector<DocCandidate> candidates;
    
    int counter = 0;
    pipeline->AddProcessor(std::make_shared<MockProcessor>("A", &counter));
    pipeline->AddProcessor(std::make_shared<MockProcessor>("B", &counter));
    pipeline->AddProcessor(std::make_shared<MockProcessor>("C", &counter));
    
    int result = pipeline->Run(session, candidates);
    
    EXPECT_EQ(result, 0);
    EXPECT_EQ(counter, 3);
    EXPECT_STREQ(session.debug_info.c_str(), "A->B->C->");
}

// 测试清空处理器
TEST_F(PipelineTest, ClearProcessorsTest) {
    int counter = 0;
    pipeline->AddProcessor(std::make_shared<MockProcessor>("proc1", &counter));
    pipeline->AddProcessor(std::make_shared<MockProcessor>("proc2", &counter));
    
    EXPECT_EQ(pipeline->GetProcessorCount(), 2);
    
    pipeline->ClearProcessors();
    EXPECT_EQ(pipeline->GetProcessorCount(), 0);
}

// 测试召回管道
TEST_F(PipelineTest, RecallPipelineTest) {
    Session session;
    session.qp_info.raw_query = "test";
    session.qp_info.terms = {"test"};
    
    std::vector<DocCandidate> candidates;
    
    // 添加两个召回处理器
    int recall_counter = 0;
    auto recall1 = std::make_shared<MockProcessor>("recall1", &recall_counter);
    auto recall2 = std::make_shared<MockProcessor>("recall2", &recall_counter);
    
    pipeline->AddProcessor(recall1);
    pipeline->AddProcessor(recall2);
    
    int result = pipeline->Run(session, candidates);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(recall_counter, 2);
}

// 测试排序管道
TEST_F(PipelineTest, RankPipelineTest) {
    Session session;
    std::vector<DocCandidate> candidates = {
        {"doc1", 0.0f, 0.0f, 0.0f, 0.0f, ""},
        {"doc2", 0.0f, 0.0f, 0.0f, 0.0f, ""},
        {"doc3", 0.0f, 0.0f, 0.0f, 0.0f, ""}
    };
    
    int rank_counter = 0;
    auto scorer = std::make_shared<MockScorerProcessor>("bm25", &rank_counter);
    
    pipeline->AddProcessor(scorer);
    int result = pipeline->Run(session, candidates);
    
    EXPECT_EQ(result, 0);
    EXPECT_EQ(rank_counter, 1);
    
    // 验证分数被修改
    for (const auto& candidate : candidates) {
        EXPECT_FLOAT_EQ(candidate.coarse_score, 1.0f);
    }
}

// 测试处理器失败时的处理
TEST_F(PipelineTest, ProcessorFailureTest) {
    Session session;
    std::vector<DocCandidate> candidates;
    
    // 添加一个会失败的处理器
    class FailingProcessor : public BaseProcessor {
    public:
        std::string Name() const override { return "FailingProcessor"; }
        bool Init(const YAML::Node& config) override { return true; }
        int Process(Session& session,
                    std::vector<DocCandidate>& candidates) override {
            return -1;  // 失败
        }
    };
    
    pipeline->AddProcessor(std::make_shared<FailingProcessor>());
    pipeline->AddProcessor(std::make_shared<MockProcessor>("after_fail", nullptr));
    
    int result = pipeline->Run(session, candidates);
    EXPECT_NE(result, 0);  // 应返回错误
}

// 测试 Pipeline 配置
TEST_F(PipelineTest, PipelineConfigTest) {
    YAML::Node config;
    config["pipeline_name"] = "test_pipeline";
    config["processor_count"] = 3;
    
    EXPECT_TRUE(pipeline->Init(config));
}

// 测试获取处理器
TEST_F(PipelineTest, GetProcessorTest) {
    auto proc1 = std::make_shared<MockProcessor>("proc1", nullptr);
    auto proc2 = std::make_shared<MockProcessor>("proc2", nullptr);
    
    pipeline->AddProcessor(proc1);
    pipeline->AddProcessor(proc2);
    
    auto retrieved = pipeline->GetProcessor(0);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->Name(), "proc1");
    
    retrieved = pipeline->GetProcessor(1);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->Name(), "proc2");
    
    // 越界
    retrieved = pipeline->GetProcessor(2);
    EXPECT_EQ(retrieved, nullptr);
}

// 测试完整搜索流程（集成测试）
TEST_F(PipelineTest, FullSearchFlowTest) {
    // 创建倒排索引
    auto index = std::make_shared<InvertedIndex>();
    std::vector<std::string> doc_ids = {"doc1", "doc2", "doc3"};
    std::vector<std::string> titles = {"Test Document 1", "Test Document 2", "Another Document"};
    std::vector<std::string> contents = {
        "This is a test document about search engines.",
        "This document is about ranking algorithms.",
        "This is another document about testing."
    };
    std::vector<std::string> categories = {"test", "test", "other"};
    std::vector<std::vector<std::string>> tags_list = {{"test"}, {"ranking"}, {"other"}};
    std::vector<int32_t> lengths = {38, 42, 38};
    
    index->AddDocuments(doc_ids, titles, contents, categories, tags_list, lengths);
    
    // 创建 Session
    Session session;
    session.qp_info.raw_query = "test document";
    session.qp_info.terms = {"test", "document"};
    
    // 创建候选文档（模拟召回结果）
    std::vector<DocCandidate> candidates = {
        {"doc1", 1.0f, 0.0f, 0.0f, 0.0f, "inverted"},
        {"doc2", 1.0f, 0.0f, 0.0f, 0.0f, "inverted"},
        {"doc3", 0.5f, 0.0f, 0.0f, 0.0f, "inverted"}
    };
    
    session.recall_results = candidates;
    
    // 创建排序管道
    int rank_counter = 0;
    pipeline->AddProcessor(std::make_shared<MockScorerProcessor>("mock_rank", &rank_counter));
    
    // 运行管道
    int result = pipeline->Run(session, candidates);
    
    EXPECT_EQ(result, 0);
    EXPECT_EQ(rank_counter, 1);
    
    // 验证候选文档分数已更新
    for (auto& candidate : candidates) {
        EXPECT_GT(candidate.coarse_score, 0.0f);
    }
}

} // namespace minisearchrec

// 主函数
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
