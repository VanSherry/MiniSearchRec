// ============================================================
// MiniSearchRec - 向量工具
// 提供向量运算、相似度计算等
// ============================================================

#ifndef MINISEARCHREC_VECTOR_UTILS_H
#define MINISEARCHREC_VECTOR_UTILS_H

#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace minisearchrec {
namespace utils {

// 向量点积
float DotProduct(const std::vector<float>& a, const std::vector<float>& b);

// 向量 L2 范数
float L2Norm(const std::vector<float>& vec);

// 余弦相似度
float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

// 欧几里得距离
float EuclideanDistance(const std::vector<float>& a, const std::vector<float>& b);

// 向量 L1 范数
float L1Norm(const std::vector<float>& vec);

// 曼哈顿距离
float ManhattanDistance(const std::vector<float>& a, const std::vector<float>& b);

// 向量归一化（L2 归一化）
void L2Normalize(std::vector<float>& vec);

// 向量加法
std::vector<float> VectorAdd(const std::vector<float>& a, const std::vector<float>& b);

// 向量减法
std::vector<float> VectorSub(const std::vector<float>& a, const std::vector<float>& b);

// 向量标量乘法
std::vector<float> VectorScale(const std::vector<float>& vec, float scalar);

// 计算向量的平均值
float Mean(const std::vector<float>& vec);

// 计算向量的标准差
float StdDev(const std::vector<float>& vec);

// 找到向量中的最大值
float Max(const std::vector<float>& vec);

// 找到向量中的最小值
float Min(const std::vector<float>& vec);

// 向量内积（批量计算，用于 Faiss 等场景）
void BatchDotProduct(const std::vector<std::vector<float>>& vectors_a,
                     const std::vector<std::vector<float>>& vectors_b,
                     std::vector<float>& results);

// 检查两个向量维度是否相同
bool SameDimension(const std::vector<float>& a, const std::vector<float>& b);

// 生成随机向量（用于测试）
std::vector<float> RandomVector(size_t dim, float min_val = -1.0f, float max_val = 1.0f);

} // namespace utils
} // namespace minisearchrec

#endif // MINISEARCHREC_VECTOR_UTILS_H
