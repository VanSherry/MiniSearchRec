// ============================================================
// MiniSearchRec - 向量工具实现
// ============================================================

#include "utils/vector_utils.h"
#include <random>
#include <algorithm>

namespace minisearchrec {
namespace utils {

float DotProduct(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector dimensions do not match");
    }
    
    float result = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        result += a[i] * b[i];
    }
    return result;
}

float L2Norm(const std::vector<float>& vec) {
    float sum_sq = 0.0f;
    for (float v : vec) {
        sum_sq += v * v;
    }
    return std::sqrt(sum_sq);
}

float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty()) {
        return 0.0f;
    }
    
    float dot = DotProduct(a, b);
    float norm_a = L2Norm(a);
    float norm_b = L2Norm(b);
    
    if (norm_a < 1e-9f || norm_b < 1e-9f) {
        return 0.0f;
    }
    
    return dot / (norm_a * norm_b);
}

float EuclideanDistance(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector dimensions do not match");
    }
    
    float sum_sq = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = a[i] - b[i];
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq);
}

float L1Norm(const std::vector<float>& vec) {
    float sum = 0.0f;
    for (float v : vec) {
        sum += std::abs(v);
    }
    return sum;
}

float ManhattanDistance(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector dimensions do not match");
    }
    
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(a[i] - b[i]);
    }
    return sum;
}

void L2Normalize(std::vector<float>& vec) {
    float norm = L2Norm(vec);
    if (norm < 1e-9f) {
        return;
    }
    for (float& v : vec) {
        v /= norm;
    }
}

std::vector<float> VectorAdd(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector dimensions do not match");
    }
    
    std::vector<float> result(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        result[i] = a[i] + b[i];
    }
    return result;
}

std::vector<float> VectorSub(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Vector dimensions do not match");
    }
    
    std::vector<float> result(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        result[i] = a[i] - b[i];
    }
    return result;
}

std::vector<float> VectorScale(const std::vector<float>& vec, float scalar) {
    std::vector<float> result(vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
        result[i] = vec[i] * scalar;
    }
    return result;
}

float Mean(const std::vector<float>& vec) {
    if (vec.empty()) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (float v : vec) {
        sum += v;
    }
    return sum / vec.size();
}

float StdDev(const std::vector<float>& vec) {
    if (vec.size() <= 1) {
        return 0.0f;
    }
    
    float mean = Mean(vec);
    float sum_sq_diff = 0.0f;
    for (float v : vec) {
        float diff = v - mean;
        sum_sq_diff += diff * diff;
    }
    return std::sqrt(sum_sq_diff / (vec.size() - 1));
}

float Max(const std::vector<float>& vec) {
    if (vec.empty()) {
        return 0.0f;
    }
    return *std::max_element(vec.begin(), vec.end());
}

float Min(const std::vector<float>& vec) {
    if (vec.empty()) {
        return 0.0f;
    }
    return *std::min_element(vec.begin(), vec.end());
}

void BatchDotProduct(const std::vector<std::vector<float>>& vectors_a,
                     const std::vector<std::vector<float>>& vectors_b,
                     std::vector<float>& results) {
    size_t n = vectors_a.size();
    results.resize(n);
    
    for (size_t i = 0; i < n; ++i) {
        results[i] = DotProduct(vectors_a[i], vectors_b[i]);
    }
}

bool SameDimension(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size();
}

std::vector<float> RandomVector(size_t dim, float min_val, float max_val) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(min_val, max_val);
    
    std::vector<float> vec(dim);
    for (size_t i = 0; i < dim; ++i) {
        vec[i] = dist(gen);
    }
    return vec;
}

} // namespace utils
} // namespace minisearchrec
