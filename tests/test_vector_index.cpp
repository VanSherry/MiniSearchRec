// ============================================================
// MiniSearchRec - 向量索引测试
// ============================================================

#include <gtest/gtest.h>
#include <cmath>
#include "../src/index/vector_index.h"
#include "../src/utils/vector_utils.h"

namespace minisearchrec {

class VectorIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        index = std::make_unique<VectorIndex>(3);  // 3 维向量，用于测试
        
        // 添加测试向量
        std::vector<std::vector<float>> vectors = {
            {1.0f, 0.0f, 0.0f},    // doc1: 纯 x 轴
            {0.0f, 1.0f, 0.0f},    // doc2: 纯 y 轴
            {0.0f, 0.0f, 1.0f},    // doc3: 纯 z 轴
            {1.0f, 1.0f, 0.0f},    // doc4: x-y 平面
            {0.5f, 0.5f, 0.5f}     // doc5: 对角线
        };
        
        std::vector<std::string> doc_ids = {"doc1", "doc2", "doc3", "doc4", "doc5"};
        
        for (size_t i = 0; i < vectors.size(); ++i) {
            index->AddVector(doc_ids[i], vectors[i]);
        }
    }
    
    std::unique_ptr<VectorIndex> index;
};

// 测试添加向量
TEST_F(VectorIndexTest, AddVectorTest) {
    EXPECT_EQ(index->GetVectorCount(), 5);
    EXPECT_EQ(index->GetDimension(), 3);
}

// 测试获取向量
TEST_F(VectorIndexTest, GetVectorTest) {
    auto vec = index->GetVector("doc1");
    ASSERT_TRUE(vec.has_value());
    EXPECT_FLOAT_EQ((*vec)[0], 1.0f);
    EXPECT_FLOAT_EQ((*vec)[1], 0.0f);
    EXPECT_FLOAT_EQ((*vec)[2], 0.0f);
}

// 测试获取不存在的向量
TEST_F(VectorIndexTest, GetNonExistentVectorTest) {
    auto vec = index->GetVector("nonexistent");
    EXPECT_FALSE(vec.has_value());
}

// 测试余弦相似度搜索
TEST_F(VectorIndexTest, CosineSearchTest) {
    // 查询向量：[1, 0, 0]（与 doc1 完全相同）
    std::vector<float> query = {1.0f, 0.0f, 0.0f};
    auto results = index->Search(query, 3, VectorMetric::COSINE);
    
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].doc_id, "doc1");  // 最相似
    EXPECT_FLOAT_EQ(results[0].score, 1.0f);  // 完全匹配，相似度 = 1
}

// 测试欧几里得距离搜索
TEST_F(VectorIndexTest, EuclideanSearchTest) {
    // 查询向量：[1, 0, 0]
    std::vector<float> query = {1.0f, 0.0f, 0.0f};
    auto results = index->Search(query, 3, VectorMetric::EUCLIDEAN);
    
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].doc_id, "doc1");  // 距离 = 0
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);  // 距离为 0
}

// 测试 L2 归一化搜索
TEST_F(VectorIndexTest, NormalizedEuclideanSearchTest) {
    // 查询向量：[1, 0, 0]
    std::vector<float> query = {1.0f, 0.0f, 0.0f};
    auto results = index->Search(query, 3, VectorMetric::EUCLIDEAN);
    
    // doc1 距离 = 0
    // doc2 距离 = sqrt(2)
    // doc4 距离 = sqrt(2)
    ASSERT_GE(results.size(), 2);
    EXPECT_LT(results[0].score, results[1].score);  // doc1 距离更小
}

// 测试点积搜索
TEST_F(VectorIndexTest, DotProductSearchTest) {
    // 查询向量：[1, 1, 0]
    std::vector<float> query = {1.0f, 1.0f, 0.0f};
    auto results = index->Search(query, 3, VectorMetric::DOT);
    
    ASSERT_FALSE(results.empty());
    // doc4 [1,1,0] 点积 = 2
    // doc1 [1,0,0] 点积 = 1
    EXPECT_EQ(results[0].doc_id, "doc4");
}

// 测试移除向量
TEST_F(VectorIndexTest, RemoveVectorTest) {
    EXPECT_TRUE(index->RemoveVector("doc1"));
    EXPECT_EQ(index->GetVectorCount(), 4);
    
    auto vec = index->GetVector("doc1");
    EXPECT_FALSE(vec.has_value());
}

// 测试清空
TEST_F(VectorIndexTest, ClearTest) {
    index->Clear();
    EXPECT_EQ(index->GetVectorCount(), 0);
}

// 测试暴力搜索与索引搜索一致性（如果实现了 HNSW）
TEST_F(VectorIndexTest, BruteForceConsistencyTest) {
    // 使用余弦相似度
    std::vector<float> query = {1.0f, 1.0f, 1.0f};
    
    // 暴力搜索
    auto brute_results = index->SearchBruteForce(query, 5, VectorMetric::COSINE);
    
    // 索引搜索（如果实现了 HNSW，结果应近似）
    auto index_results = index->Search(query, 5, VectorMetric::COSINE);
    
    // 至少结果数量应相同
    EXPECT_EQ(brute_results.size(), index_results.size());
    
    // 第一个结果应相同（最可能的结果）
    if (!brute_results.empty() && !index_results.empty()) {
        EXPECT_EQ(brute_results[0].doc_id, index_results[0].doc_id);
    }
}

// 测试高维向量
TEST_F(VectorIndexTest, HighDimensionalTest) {
    VectorIndex high_dim_index(128);  // 128 维
    
    // 添加随机向量
    for (int i = 0; i < 100; ++i) {
        std::vector<float> vec = utils::RandomVector(128, -1.0f, 1.0f);
        high_dim_index.AddVector("doc" + std::to_string(i), vec);
    }
    
    EXPECT_EQ(high_dim_index.GetVectorCount(), 100);
    
    // 搜索
    std::vector<float> query = utils::RandomVector(128, -1.0f, 1.0f);
    auto results = high_dim_index.Search(query, 10, VectorMetric::COSINE);
    
    EXPECT_FALSE(results.empty());
    EXPECT_LE(results.size(), 10);
}

// 测试度量类型转换
TEST_F(VectorIndexTest, MetricTypeTest) {
    EXPECT_EQ(GetMetricName(VectorMetric::COSINE), "cosine");
    EXPECT_EQ(GetMetricName(VectorMetric::EUCLIDEAN), "euclidean");
    EXPECT_EQ(GetMetricName(VectorMetric::DOT), "dot");
    
    EXPECT_EQ(GetMetricFromName("cosine"), VectorMetric::COSINE);
    EXPECT_EQ(GetMetricFromName("euclidean"), VectorMetric::EUCLIDEAN);
    EXPECT_EQ(GetMetricFromName("dot"), VectorMetric::DOT);
}

} // namespace minisearchrec

// 主函数
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
