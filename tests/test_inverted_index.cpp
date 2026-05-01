// ============================================================
// MiniSearchRec - 倒排索引单元测试
// ============================================================

#include <gtest/gtest.h>
#include "index/inverted_index.h"

namespace minisearchrec {

class InvertedIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        idx_ = std::make_unique<InvertedIndex>();
    }

    std::unique_ptr<InvertedIndex> idx_;
};

TEST_F(InvertedIndexTest, AddAndSearch) {
    idx_->AddDocument("doc1", "深度学习入门", "本文介绍深度学习的基础概念...",
                    "tech", {"深度学习", "AI"}, 50);
    idx_->AddDocument("doc2", "机器学习实战", "本文介绍机器学习的实战技巧...",
                    "tech", {"机器学习", "Python"}, 50);

    // OR 搜索
    auto results = idx_->Search({"深度", "机器学习"}, 100);
    EXPECT_GE(results.size(), 1);
}

TEST_F(InvertedIndexTest, CalculateIDF) {
    idx_->AddDocument("doc1", "A", "hello world", "tech", {"test"}, 11);
    idx_->AddDocument("doc2", "B", "hello", "tech", {"test"}, 5);

    float idf_hello = idx_->CalculateIDF("hello");
    float idf_world = idx_->CalculateIDF("world");

    // "hello" 出现在2篇文档中，IDF 应该较低
    // "world" 出现在1篇文档中，IDF 应该较高
    EXPECT_GT(idf_world, idf_hello);
    EXPECT_GT(idf_hello, 0.0f);
}

TEST_F(InvertedIndexTest, GetAvgDocLen) {
    idx_->AddDocument("doc1", "A", "short", "tech", {}, 5);
    idx_->AddDocument("doc2", "B", "a very long document here", "tech", {}, 25);

    float avg = idx_->GetAvgDocLen();
    EXPECT_FLOAT_EQ(avg, 15.0f);
}

TEST_F(InvertedIndexTest, SaveAndLoad) {
    idx_->AddDocument("doc1", "Test", "hello world test", "tech", {"test"}, 16);
    idx_->AddDocument("doc2", "Test2", "world test", "tech", {"test"}, 10);

    std::string path = "/tmp/test_index.idx";
    EXPECT_TRUE(idx_->Save(path));

    InvertedIndex idx2;
    EXPECT_TRUE(idx2.Load(path));
    EXPECT_EQ(idx2.GetDocCount(), 2);
    EXPECT_EQ(idx2.GetTermCount(), 3);  // hello, world, test

    std::remove(path.c_str());
}

} // namespace minisearchrec
