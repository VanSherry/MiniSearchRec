// ============================================================
// MiniSearchRec - A/B 实验框架
// 参考：微信 XExpt、X(Twitter) FeatureSwitch
// ============================================================

#ifndef MINISEARCHREC_AB_TEST_H
#define MINISEARCHREC_AB_TEST_H

#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include <functional>

namespace minisearchrec {

// ============================================================
// 实验配置
// ============================================================
struct ExperimentConfig {
    std::string name;                  // 实验名
    float traffic_ratio = 0.0f;        // 流量占比（0.0 - 1.0）
    std::string bucket_method = "hash";  // 分桶方法：hash / random
    std::map<std::string, std::string> params;  // 实验参数覆盖
};

// ============================================================
// A/B 实验管理器
// 参考：微信 XExpt 实验框架
// ============================================================
class ABTestManager {
public:
    ABTestManager() = default;
    ~ABTestManager() = default;

    // 从 YAML 加载实验配置
    bool LoadFromYAML(const YAML::Node& config);
    bool LoadFromFile(const std::string& path);

    // 根据用户 ID 分配实验组
    // 返回：实验配置指针，若在对照则返回 nullptr
    const ExperimentConfig* AssignExperiment(const std::string& uid) const;

    // 获取参数值（实验组覆盖 > 默认值）
    std::string GetParam(const std::string& uid,
                         const std::string& key,
                         const std::string& default_value) const;

    // 获取所有实验名
    std::vector<std::string> ListExperiments() const;

private:
    // 一致性 Hash 分桶
    uint32_t ConsistentHash(const std::string& uid) const;

    std::vector<ExperimentConfig> experiments_;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_AB_TEST_H
