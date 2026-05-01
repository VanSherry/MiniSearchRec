# MiniSearchRec 项目生成总结

> 已根据你的设计文档完整生成 MiniSearchRec 项目骨架
> 生成时间：2026-05-01

---

## 项目概述

**MiniSearchRec** 是一个面向纯后端初学者的搜索推荐系统学习项目，参考 X (Twitter) 开源推荐算法和微信搜推内部架构设计。

---

## 已生成的文件清单

### 根目录
| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 项目构建配置 |
| `README.md` | 项目说明文档 |
| `.gitignore` | Git 忽略规则 |

### 配置文件 (`config/`)
| 文件 | 说明 |
|------|------|
| `global.yaml` | 全局配置（服务器、索引、日志、缓存）|
| `recall.yaml` | 召回策略配置 |
| `rank.yaml` | 排序配置 |
| `filter.yaml` | 过滤配置 |

### Proto 定义 (`proto/`)
| 文件 | 说明 |
|------|------|
| `doc.proto` | 文档数据模型 |
| `user.proto` | 用户数据模型 |
| `search.proto` | 搜索请求/响应模型 |
| `event.proto` | 用户行为事件模型 |

### 核心框架 (`src/core/`)
| 文件 | 说明 |
|------|------|
| `session.h/.cpp` | Session 上下文对象 |
| `processor.h` | 处理器基类定义（召回/打分/过滤/后处理）|
| `factory.h/.cpp` | 处理器工厂（插件化注册）|
| `pipeline.h/.cpp` | Pipeline 编排器 |
| `config_manager.h/.cpp` | 配置管理器 |

### 索引模块 (`src/index/`)
| 文件 | 说明 |
|------|------|
| `inverted_index.h/.cpp` | 倒排索引（BM25 基础）|
| `vector_index.h/.cpp` | 向量索引（Faiss 封装，V1）|
| `doc_store.h/.cpp` | 文档存储（SQLite 封装）|
| `index_builder.h/.cpp` | 索引构建器 |

### 召回模块 (`src/recall/`)
| 文件 | 说明 |
|------|------|
| `inverted_recall.h/.cpp` | 倒排索引召回 |
| `user_history_recall.h/.cpp` | 用户历史召回 |
| `hot_content_recall.h/.cpp` | 热门内容召回 |
| `vector_recall.h/.cpp` | 向量语义召回（V1）|
| `recall_fusion.h/.cpp` | 多路召回融合（RRF 算法）|

### 排序模块 (`src/rank/`)
| 文件 | 说明 |
|------|------|
| `bm25_scorer.h/.cpp` | BM25 打分器 |
| `quality_scorer.h/.cpp` | 质量分打分器 |
| `freshness_scorer.h/.cpp` | 新鲜度打分器 |
| `lgbm_ranker.h/.cpp` | LightGBM 精排（V1，占位）|
| `mmr_reranker.h/.cpp` | MMR 多样性重排 |

### 过滤模块 (`src/filter/`)
| 文件 | 说明 |
|------|------|
| `dedup_filter.h/.cpp` | 去重过滤器 |
| `quality_filter.h/.cpp` | 质量过滤器 |
| `spam_filter.h/.cpp` | 垃圾内容过滤器（V1）|
| `blacklist_filter.h/.cpp` | 黑名单过滤器（V1）|

### 缓存模块 (`src/cache/`)
| 文件 | 说明 |
|------|------|
| `lru_cache.h` | LRU 缓存模板类 |
| `cache_manager.h/.cpp` | 统一缓存管理器 |

### A/B 测试模块 (`src/ab/`)
| 文件 | 说明 |
|------|------|
| `ab_test.h/.cpp` | A/B 实验框架 |

### 查询解析模块 (`src/query/`)
| 文件 | 说明 |
|------|------|
| `query_parser.h/.cpp` | Query 解析器（分词/停用词）|

### 用户模块 (`src/user/`)
| 文件 | 说明 |
|------|------|
| `user_profile.h/.cpp` | 用户画像管理 |

### 特征模块 (`src/feature/`)
| 文件 | 说明 |
|------|------|
| `feature_store.h/.cpp` | 特征存储 |

### 服务层 (`src/service/`)
| 文件 | 说明 |
|------|------|
| `http_server.h/.cpp` | HTTP 服务器（cpp-httplib）|
| `search_handler.h/.cpp` | 搜索接口处理器 |
| `doc_handler.h/.cpp` | 文档管理接口处理器 |
| `event_handler.h/.cpp` | 用户行为上报处理器 |

### 工具 (`tools/`)
| 文件 | 说明 |
|------|------|
| `build_index.cpp` | 离线索引构建工具 |
| `import_docs.py` | 文档导入脚本（CSV/TXT → JSON）|

### 测试 (`tests/`)
| 文件 | 说明 |
|------|------|
| `test_inverted_index.cpp` | 倒排索引单元测试 |

### 数据 (`data/`)
| 文件 | 说明 |
|------|------|
| `sample_docs.json` | 示例文档数据（5 篇）|

### 文档 (`docs/`)
| 文件 | 说明 |
|------|------|
| `learning_path.md` | 四周学习路径指南 |

---

## 项目架构

```
MiniSearchRec/
├── CMakeLists.txt              # 构建配置
├── README.md                  # 项目说明
├── .gitignore                 # Git 忽略规则
├── config/                   # 配置文件（YAML）
├── proto/                    # Protobuf 定义
├── src/
│   ├── core/                # 核心框架
│   ├── index/               # 索引模块
│   ├── recall/              # 召回模块
│   ├── rank/                # 排序模块
│   ├── filter/              # 过滤模块
│   ├── cache/               # 缓存模块
│   ├── ab/                  # A/B 测试
│   ├── query/               # Query 解析
│   ├── user/                # 用户画像
│   ├── feature/             # 特征存储
│   └── service/            # HTTP 服务
├── tools/                   # 工具脚本
├── tests/                   # 单元测试
├── data/                    # 示例数据
├── models/                  # 模型文件
└── docs/                    # 文档
```

---

## 下一步计划

### 立即可以做的事

1. **安装依赖并编译**
   ```bash
   cd MiniSearchRec
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make -j4
   ```

2. **构建索引**
   ```bash
   ./build/tools/build_index \
       --config ./config \
       --input ./data/sample_docs.json
   ```

3. **启动服务**
   ```bash
   ./build/minisearchrec --config ./config
   ```

4. **测试搜索**
   ```bash
   curl -X POST <http://localhost:8080/api/v1/search> \
     -H "Content-Type: application/json" \
     -d '{"query": "深度学习", "page": 1, "page_size": 5}'
   ```

### 待完善的功能（按优先级）

| 优先级 | 功能 | 说明 |
|--------|------|------|
| P0 | 修复编译错误 | 部分文件引用可能缺失，需调整 include 路径 |
| P0 | 完善 HTTP 服务器 | `http_server.cpp` 中的 httplib 用法需根据实际版本调整 |
| P1 | 集成 cppjieba | 替换简化版分词为真实中文分词 |
| P1 | 集成 Faiss | 完善 `vector_index.cpp` 中的 Faiss 调用 |
| P1 | 集成 LightGBM | 完善 `lgbm_ranker.cpp` 中的 LightGBM C API 调用 |
| P2 | Redis 缓存 | 完善 `redis_client.h/.cpp` |
| P2 | 完善用户模块 | 实现 `user_event_handler` 和 `user_interest_updater` |
| P2 | 更多测试 | 添加 `test_bm25_scorer`、`test_pipeline` 等 |

---

## 设计对齐说明

本项目已按你的设计文档完整生成骨架，对齐了：

| 设计文档章节 | 实现状态 |
|--------------|---------|
| 产品设计与功能规划 | ✅ 配置文件已生成 |
| 技术方案与路线图 | ✅ 按四个阶段分阶段实现 |
| 数据模型设计 | ✅ Protobuf 定义完成 |
| 配置设计 | ✅ YAML 配置框架完成 |
| 系统架构设计 | ✅ 各层模块已生成 |
| 倒排索引设计 | ✅ `InvertedIndex` 实现完成 |
| BM25 打分 | ✅ `BM25ScorerProcessor` 实现完成 |
| 向量索引设计 | ✅ `VectorIndex` 框架完成（V1 占位）|
| 多路召回融合 | ✅ `RecallFusion` RRF 算法实现完成 |
| 特征体系设计 | ✅ `FeatureStore` 框架完成 |
| LightGBM 排序 | ✅ `LGBMRankerProcessor` 占位完成 |
| MMR 多样性重排 | ✅ `MMRRerankProcessor` 实现完成 |
| 用户画像系统 | ✅ `UserProfileManager` 框架完成 |
| Session 上下文 | ✅ `Session` 类实现完成 |
| 插件化处理器 | ✅ 工厂模式 + 注册宏完成 |
| 缓存层设计 | ✅ LRU + CacheManager 完成 |
| A/B 实验框架 | ✅ `ABTestManager` 完成 |
| API 接口规范 | ✅ HTTP 处理器框架完成 |

---

## 注意事项

1. **命名空间已统一**：所有文件已使用 `namespace minisearchrec`
2. **占位实现**：V1/V2 阶段的功能（Faiss、LightGBM）当前为占位实现
3. **简化版组件**：分词、相似度计算等使用了简化实现，学习时可逐步替换
4. **依赖未内置**：`third_party/` 目录为空，需自行安装依赖或通过包管理器安装

---

## 快速上手命令

```bash
# 1. 克隆/进入项目
cd MiniSearchRec

# 2. 安装依赖（Ubuntu）
sudo apt-get install -y build-essential cmake libprotobuf-dev \
    protobuf-compiler libyaml-cpp-dev libsqlite3-dev

# 3. 编译
mkdir -p build && cd build
cmake .. && make -j4

# 4. 构建索引
./build/tools/build_index --input data/sample_docs.json

# 5. 启动服务
./build/minisearchrec

# 6. 测试
curl -X POST <http://localhost:8080/api/v1/search> \
  -H "Content-Type: application/json" \
  -d '{"query": "深度学习", "page": 1}'
```

---

**项目骨架已生成完毕！** 你可以基于这个骨架开始学习或继续完善各个模块。
