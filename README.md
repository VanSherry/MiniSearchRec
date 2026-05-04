<div align="center">

# MiniSearchRec

**生产级 C++17 搜索推荐引擎**

*配置驱动 · 框架优先 · 零改代码扩展业务 · RankEngine 纯计算引擎*

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=flat-square&logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.16+-064F8C.svg?style=flat-square&logo=cmake)](https://cmake.org/)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=flat-square)](LICENSE)
[![Tests](https://img.shields.io/badge/Tests-100%2B_passed-brightgreen.svg?style=flat-square)](#测试)
[![Lines](https://img.shields.io/badge/C%2B%2B_Lines-16%2C600%2B-informational.svg?style=flat-square)](#)
[![Homepage](https://img.shields.io/badge/Homepage-GitHub_Pages-222.svg?style=flat-square&logo=github)](https://vansherry.github.io/MiniSearchRec/)
[![Live Demo](https://img.shields.io/badge/Live_Demo-vansherry.online-07c160.svg?style=flat-square&logo=googlechrome)](https://vansherry.online/v2)

<br/>

单进程内嵌 **索引构建 · 查询理解 · DAG 并行纯召回 · RankEngine 多级排序 · 用户画像 · AB实验 · 在线训练 · 数据飞轮闭环**<br/>
完整展示现代搜索推荐系统从 Query 到 Result 到 Feedback 的完整运作原理

<br/>

[**🔍 在线体验**](https://vansherry.online/v2) · [**📄 项目主页**](https://vansherry.github.io/MiniSearchRec/) · [快速开始](#-快速开始) · [系统架构](#-系统架构) · [交互闭环](#-用户交互闭环) · [技术亮点](#-技术亮点) · [API 文档](#-api-接口)

</div>

---

## ✨ 技术亮点

<table>
<tr>
<td width="50%">

### 🏗️ 架构设计
- **Template Method** 主流程骨架（8 阶段 Pipeline）
- **反射注册** 宏 — 新增 Processor 只需 1 行注册
- **YAML 驱动** — 所有业务路由/Pipeline/定时任务均为配置
- **三层分离** — `framework/` → `biz/` → `lib/`
- **RankEngine 纯计算引擎** — 只处理 `RankInput→RankOutput`，不碰 session I/O，主框架负责读写 session，所有业务共享同一引擎

### 🔍 搜索能力
- **12 路 DAG 并行纯召回**：Search 4 路 + Nav 3 路 + Hint 4 路 + Sug 1 路，每路独立 DAG 节点并行执行，纯召回不打分（分数由 RankEngine 处理）
- **KV 特征化排序**：`RankItem::features` 存储特征，Processor 通过 `GetFeature/SetFeature` 读写，新增特征只需在 BuildRankInput 中加一行
- **可复用排序算子**：BM25Rank / QualityRank / FreshnessRank / LGBMRank / WeightedSumScorer 均注册为 `REGISTER_RANK_PROCESSOR`，任何业务 YAML 配置即可使用

</td>
<td width="50%">

### 🧠 智能特性
- **内置 Embedding**：bge-base-zh-v1.5 ONNX 模型（768 维）
- **查询理解**：全角归一化 / 分词 / 同义词扩展 / 类别推断
- **用户画像**：实时 EMA 兴趣向量更新 + 类别权重
- **AB 实验**：UID 哈希分流，参数覆盖到任意 Pipeline 阶段

### ⚡ 工程质量
- **双 Buffer 热更新**：模型 + 索引无锁原子切换
- **多级缓存**：本地 LRU + Redis（可选）
- **后台调度器**：自动训练 / 索引重建 / Trie 重建
- **117/117 测试通过**，覆盖框架层 + 业务层 + 算子层

</td>
</tr>
</table>

---

## 🏛️ 系统架构

```
┌──────────────────────────────────────────────────────────────────┐
│                     gateway/ (HTTP 网关 · cpp-httplib)            │
├──────────┬──────────┬──────────┬──────────┬──────────────────────┤
│  search  │   sug    │   hint   │   nav    │   doc / event / admin│
│  (垂搜)  │(下拉联想) │ (点后推) │(教育页) │   (文档·行为·管理)    │
├──────────┴──────────┴──────────┴──────────┴──────────────────────┤
│                     framework/ (框架层 · 零改扩展)                 │
│  ┌────────────┐ ┌────────────┐ ┌──────────────┐ ┌────────────┐  │
│  │  Handler   │ │  Session   │ │  Processor   │ │  Server    │  │
│  │  Manager   │ │  Factory   │ │  Pipeline    │ │  Router    │  │
│  └────────────┘ └────────────┘ └──────────────┘ └────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │  RankEngine — 纯计算排序引擎                               │   │
│  │  BuildRankInput(session)  →  RankItem[]                  │   │
│  │  RankEngine::Score(items)  →  Processor 链打分           │   │
│  │  ApplyRankOutput(session)  ←  排序截断写回               │   │
│  │  主框架负责 session I/O，RankEngine 只做纯计算            │   │
│  └──────────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────────┤
│                     lib/ (公共算子库 · 各业务按需调用)               │
│  index/  query/  recall/(DAG)  rank/(scorer+reranker)  filter/  │
│  embedding/  storage/  user/  feature/                           │
├──────────────────────────────────────────────────────────────────┤
│  scheduler/ (定时任务)  │  cache/ (多级缓存)  │  ab/ (实验框架)     │
└─────────────────────────┴─────────────────────┴──────────────────┘
```

---

## 🔄 用户交互闭环

MiniSearchRec 实现了搜索产品的 **完整交互闭环**（参考微信搜一搜）：

```
                        用户打开搜索框
                              │
                    ┌─────────▼─────────┐
                    │       Nav         │  GET /nav
                    │    教育页      │  热词推荐（6 条）
                    │ 全局热词+用户历史   │  数据源: QueryStats + DocStore
                    └────┬─────────┬────┘
                         │         │
              点击热词     │         │  开始打字
          ┌──────────────┘         └──────────────────────┐
          │                                                │
          │                                     ┌──────────▼──────────┐
          │                                     │        Sug          │
          │                                     │    下拉联想（8 条）   │ GET /sug?q=前缀
          │                                     │  Trie 前缀匹配       │
          │                                     │ 文档标题+标签+历史词   │
          │                                     └──────────┬──────────┘
          │                                                │ 点击建议词/回车
          │                                                │
          ▼────────────────────────────────────────────────▼
                              │
                    ┌─────────▼─────────┐
                    │      Search       │  POST /search
                    │   搜索结果页(首次)  │
                    │   约 20 条结果 Box  │  首次无 Hint
                    └─────────┬─────────┘
                              │ 用户点击某个 Box
                              │
                    ┌─────────▼─────────┐
                    │   上报 click 事件   │  POST /event/click
                    │   进入文档详情页     │  携带 uid + doc_id + query
                    └─────────┬─────────┘
                              │ 用户点击返回
                              │
                    ┌─────────▼─────────┐
                    │ 搜索结果页(带 Hint) │
                    │                   │
                    │  在点击的 Box 下方   │  GET /hint?doc_id=xxx&query=...
                    │  展示 Hint 词条     │  "大家都在搜"
                    │  标签匹配+行为共现   │  根据布局动态适配条数
                    └────┬─────────┬────┘
                         │         │
              点击 Hint   │         │  继续浏览/点击其他 Box
                         │         └──→ 上报 → 详情 → 返回 → 更多 Hint
                         │
                         └──→ 新 query 重新搜索（回到首次搜索，无 Hint）
```

> **数据飞轮**：用户每次交互产生 5 路数据写入，后台定时任务（训练/索引重建/Trie重建）不断将数据反哺到各业务，使搜索效果持续提升。

---

## 📐 搜索主流程（Template Method · RankEngine 纯计算驱动）

```
BaseHandler::Search(session)                    ← 8 阶段 Pipeline 骨架
    │
    ├── 0. InitSession          初始化 Session + TraceID + 超时控制
    │
    ├── 1. CanSearch             准入检查 + InterposeCheckQuery（封禁词）
    │
    ├── 2. PreSearch             QueryParser::Parse()
    │       ├── Normalize        全角→半角 → 小写 → 去噪 → 纠错 → 去停用词
    │       ├── Tokenize         分词（cppjieba / 简单分词降级）
    │       ├── Expand           同义词 + 相关词 + 缩写 + 类别词
    │       ├── InferCategory    关键词→类别推断
    │       └── Embedding        bge-base-zh ONNX / 伪向量降级
    │
    ├── 3. DoSearch              ← DAG 并行召回 (recall_stages)
    │       ├── InvertedRecall    倒排索引 · 纯召回
    │       ├── VectorRecall      向量近邻 · 纯召回
    │       ├── HotContentRecall  热榜缓存 · CAS 原子刷新
    │       └── UserHistoryRecall 用户历史 · query 相关性过滤
    │       MergeRecall: 聚合各 DAG 输出 → session
    │
    ├── 4. DoRank (粗排)         ← RankEngine 纯计算
    │       [主框架 I/O]          BuildRankInput(session) → RankItem[]
    │       [引擎计算]            RankEngine::Score(items) → BM25+Quality+Freshness
    │       [主框架 I/O]          ApplyRankOutput(session, items, "rank")
    │       AfterRank             sort → truncate top-500 → coarse_rank_results
    │
    ├── 5. DoRerank (精排)       ← RankEngine 纯计算（复用 BuildRankInput/ApplyRankOutput）
    │       [主框架 I/O]          BuildRankInput(session, "rerank") → coarse_rank_results
    │       [引擎计算]            RankEngine::Score(items) → LGBMRankProcessor
    │       [主框架 I/O]          ApplyRankOutput(session, items, "rerank")
    │       AfterRerank           sort → truncate top-100 → fine_rank_results
    │
    ├── 6. DoInterpose           ← filter + postprocess pipeline
    │       ├── DedupFilter      UTF-8 字符级 Jaccard ≥ 0.9
    │       ├── QualityFilter    quality / click / content_length 过滤
    │       ├── SpamFilter       重复字符 / 全大写检测
    │       ├── BlacklistFilter  doc_id / author 黑名单
    │       └── MMRReranker      λ=0.7 多样性重排 → top-20
    │
    ├── 7. SetResponse           分页截取 + JSON 序列化
    │
    └── 8. ReportFinal           各阶段耗时日志（ScopeGuard 保证执行）
```

---

## 📁 项目结构

```
MiniSearchRec/
├── config/
│   ├── framework.yaml              # 框架配置（服务器/Embedding/调度器/路由表）
│   └── biz/                        # 各业务独立 Pipeline + Rank 配置
│       ├── search.yaml             #   DAG(4路) → rank_config(BM25+Quality+Freshness+LGBM) → filter(4) → post
│       ├── sug.yaml                #   DAG(1路Trie) → rank_config(WeightedSumScorer)
│       ├── hint.yaml               #   DAG(4路标签+共现+分类+扩展) → rank_config(WeightedSumScorer)
│       └── nav.yaml                #   DAG(3路热词+标题+预置) → rank_config(WeightedSumScorer)
├── src/
│   ├── framework/                  # 框架层（新增业务不碰此目录）
│   │   ├── handler/                #   BaseHandler + HandlerManager
│   │   ├── session/                #   Session 生命周期 + KV/Any 存储
│   │   ├── processor/              #   DagPipeline + ProcessorPipeline + ProcessorInterface
│   │   ├── server/                 #   统一请求路由
│   │   ├── config/                 #   ConfigManager
│   │   ├── app_context.h           #   全局 DI 容器（线程安全）
│   │   └── class_register.h        #   反射注册宏
│   ├── biz/                        # 业务实现层
│   │   ├── search/                 #   全文搜索（SearchSession + DocCandidate）
│   │   │   └── search_handler      #   BuildRankInput 预计算 10+ 特征
│   │   ├── sug/                    #   搜索建议（Trie 双 Buffer）
│   │   ├── hint/                   #   点后推荐（DocCooccurStore）
│   │   ├── nav/                    #   教育页（热词召回 + 预置词兜底）
│   │   ├── doc/                    #   文档 CRUD API
│   │   └── event/                  #   事件接入（5 路数据写入）
│   ├── lib/                        # 公共算子库
│   │   ├── index/                  #   InvertedIndex(thread-safe) + VectorIndex(Faiss/暴力) + DocStore(thread_local)
│   │   ├── query/                  #   QueryParser → Normalizer → Expander → Embedding
│   │   ├── recall/                 #   DAG 召回算子（12 路纯召回，分数由 RankEngine 处理）
│   │   ├── rank/engine/            #   RankEngine(纯计算) + RankItem + RankConfigManager + ProcessorInterface
│   │   ├── rank/scorer/            #   BM25Rank / QualityRank / FreshnessRank / LGBMRank / WeightedSumScorer
│   │   ├── rank/reranker/          #   MMR 多样性重排
│   │   ├── filter/                 #   Dedup / Quality / Spam / Blacklist
│   │   ├── embedding/              #   ONNX bge-base-zh / Pseudo 降级
│   │   ├── storage/                #   QueryStatsStore + DocCooccurStore
│   │   └── user/                   #   UserProfile(Proto) + UserEventHandler
│   ├── scheduler/                  # 后台调度器（单线程事件循环）
│   │   └── task/                   #   AutoTrain(24h) / IndexRebuild(12h) / TrieRebuild(1h)
│   ├── gateway/                    # HTTP 网关（cpp-httplib）
│   ├── cache/                      # 双层缓存（LRU + Redis）
│   ├── ab/                         # AB 实验框架（UID 哈希分流）
│   └── utils/                      # 日志 / 字符串(UTF-8) / 哈希 / 向量运算
├── proto/                          # Protobuf 定义（SearchRequest/Response, UserProfile, Document）
├── models/bge-base-zh/             # 内置 ONNX Embedding 模型（99MB）
├── tests/                          # 集成测试（100+ cases）
└── data/                           # 示例文档数据
```

> **153 个源文件 · 16,600+ 行 C++17 代码**

---

## 🚀 快速开始

### 编译构建

```bash
# 克隆项目
git clone https://github.com/VanSherry/MiniSearchRec.git
cd MiniSearchRec/MiniSearchRec

# 编译（默认不构建测试，加快编译）
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 启动服务
./minisearchrec --config ./config

# 如需运行测试
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make test_all -j$(nproc)
./test_all    # 预期：PASS=117, FAIL=0
```

### 依赖项

| 库 | 用途 | 是否必需 |
|----|------|:--------:|
| **yaml-cpp** | 配置解析 | ✅ |
| **protobuf** | 数据序列化 | ✅ |
| **jsoncpp** | JSON API | ✅ |
| **sqlite3** | 文档/统计存储 | ✅ |
| spdlog | 结构化日志 | 可选 |
| ONNX Runtime | Embedding 推理 | 可选（降级到伪向量） |
| LightGBM | 精排模型 | 可选（降级到规则决策树） |
| Faiss | 向量近似搜索 | 可选（降级到暴力搜索） |

---

## 📡 API 接口

### 搜索类

| 接口 | 方法 | 说明 | 关键参数 |
|------|:----:|------|----------|
| `/api/v1/search` | POST | 全文搜索 | `query`, `uid`, `page`, `page_size` |
| `/api/v1/sug` | GET | 搜索建议（下拉联想） | `q`（前缀） |
| `/api/v1/hint` | GET | 点后推荐（用户点击文档返回后展示） | `doc_id`（必填）, `query`（可选） |
| `/api/v1/nav` | GET | 教育页（搜前热词引导） | `uid`（可选，个性化） |

### 数据管理

| 接口 | 方法 | 说明 |
|------|:----:|------|
| `/api/v1/doc/add` | POST | 添加文档（同时建索引） |
| `/api/v1/doc/update` | PUT | 更新文档 |
| `/api/v1/doc/delete` | DELETE | 删除文档 |
| `/api/v1/event/click` | POST | 上报点击事件 |
| `/api/v1/event/like` | POST | 上报点赞事件 |
| `/api/v1/admin/reload_model` | POST | 精排模型热更新 |
| `/health` | GET | 健康检查 + 索引统计 |

### 请求示例

```bash
# 搜索
curl -X POST http://localhost:8080/api/v1/search \
  -H 'Content-Type: application/json' \
  -d '{"query": "人工智能", "uid": "user_001", "page": 1, "page_size": 20}'

# 搜索建议
curl "http://localhost:8080/api/v1/sug?q=人工"

# 添加文档
curl -X POST http://localhost:8080/api/v1/doc/add \
  -H 'Content-Type: application/json' \
  -d '{"doc_id": "doc_001", "title": "深度学习入门", "content": "...", "category": "technology"}'

# 上报点击
curl -X POST http://localhost:8080/api/v1/event/click \
  -H 'Content-Type: application/json' \
  -d '{"uid": "user_001", "doc_id": "doc_001", "query": "深度学习", "result_pos": 0}'
```

---

## ➕ 新增业务（零改框架代码）

只需 **3 步**，无需修改 `framework/` 或 `main.cpp`：

```bash
# Step 1：编写 Handler
vim src/biz/xxx/xxx_handler.cpp
# 继承 BaseHandler，覆写 BuildRankInput / ApplyRankOutput（或使用默认）
# 末尾：REGISTER_MSR_HANDLER(XxxBizHandler);

# Step 2：编写 Processor（可选，也可以直接用通用的 WeightedSumScorer）
vim src/lib/rank/scorer/xxx_processor.cpp
# 继承 rank::ProcessorInterface，实现 Init + Process
# 末尾：REGISTER_RANK_PROCESSOR(XxxProcessor);

# Step 3：添加配置
vim config/biz/xxx.yaml           # recall_stages(DAG) + rank_config{rank:{processors}}
vim config/framework.yaml         # businesses[] 中加一条路由

# 重新编译启动 —— 完成！
```

**简化场景**：如果业务不需要自定义排序规则，直接用默认的 `BuildRankInput`（从 `any_store` 读取 `{biz}_recall_docs`）和 `ApplyRankOutput`（写入 `{biz}_rank_vector`）+ `WeightedSumScorer`，只需写 YAML 配置，一行 C++ 代码都不需要写。

---

## 🎨 设计模式一览

| 模式 | 应用 | 效果 |
|------|------|------|
| **Template Method** | `BaseHandler::Search()` 8 阶段骨架 | 业务只需覆写钩子 |
| **Strategy** | EmbeddingProvider / 各 Processor | 配置一键切换实现 |
| **Pipeline** | `ProcessorPipeline` 链式执行 | YAML 驱动编排 |
| **Registry + Reflection** | `REGISTER_MSR_*` / `REGISTER_RANK_PROCESSOR` 宏 | 配置驱动零代码注册 |
| **Double Buffer** | LGBMRankProcessor / AppContext::SwapIndexes | 无锁原子切换 |
| **DI Container** | `AppContext` 全局单例 | 解耦资源依赖 |
| **Scope Guard** | `BaseHandler::Search()` 出口 | 保证日志上报 |
| **Observer** | EventHandler → 5 路数据写入 | 解耦事件处理 |

---

## 📊 性能参考

| 指标 | 数值 | 条件 |
|------|------|------|
| 端到端延迟 | < 50ms | 1000 篇文档，单机 |
| 倒排召回 | < 5ms | 1000 篇，5 个查询词 |
| BM25 打分 | < 2ms | 500 篇候选 |
| LGBM 精排 | < 10ms | 100 篇候选（内置规则树） |
| Embedding | ~15ms/条 | ONNX Runtime CPU |
| 索引构建 | < 1s | 1000 篇全量重建 |

---

## ⚙️ 配置参数调参指南

所有参数均在 YAML 中配置，修改后重启服务即生效，无需改代码。

### Search Pipeline（`config/biz/search.yaml`）

#### 召回阶段（DAG 并行，纯召回不打分）

| Processor | 参数 | 默认值 | 建议范围 | 说明 |
|-----------|------|:------:|:--------:|------|
| **InvertedRecall** | `max_recall` | 1000 | 500~5000 | 倒排召回上限，文档量大时适当提高 |
| | `min_term_freq` | 1 | 1~3 | 最低词频阈值，提高可减少噪声但可能漏召回 |
| **VectorRecall** | `enable` | true | — | 需要 Faiss/向量索引，无索引时自动跳过 |
| | `top_k` | 200 | 100~500 | 向量近邻数量 |
| | `similarity_threshold` | 0.3 | 0.2~0.6 | 语义相似度阈值，越高越精准但召回越少 |
| | `embedding_dim` | 768 | — | 须与 Embedding 模型维度一致（bge-base-zh = 768） |
| **UserHistoryRecall** | `max_recall` | 200 | 50~500 | 用户历史召回上限 |
| | `history_window_days` | 30 | 7~90 | 历史行为窗口（天） |
| **HotContentRecall** | `max_recall` | 100 | 50~200 | 热门内容召回上限 |
| | `time_window_hours` | 24 | 6~72 | 热门内容时间窗口（小时） |
| | `refresh_interval_sec` | 300 | 60~600 | 热榜缓存刷新间隔（秒） |

#### Rank 阶段（由 RankEngine 纯计算引擎驱动）

配置格式（`rank_config.rank` / `rank_config.rerank`，`REGISTER_RANK_PROCESSOR` 注册）：

| 阶段 | Processor | weight | 特征来源 | 说明 |
|:----:|-----------|:------:|----------|------|
| rank | **BM25RankProcessor** | 0.6 | BuildRankInput 预计算 `"bm25"` | tanh(BM25/10) 归一化 |
| rank | **QualityRankProcessor** | 0.2 | BuildRankInput 预计算 `"quality"` | click + like + quality 加权 |
| rank | **FreshnessRankProcessor** | 0.2 | BuildRankInput 预计算 `"freshness"` | exp(-0.01 × age_days) |
| rerank | **LGBMRankProcessor** | 0.8 | 10 维 KV 特征 | LightGBM / 内置规则树（双 Buffer 热更新） |

**主框架 I/O 流程**：
```
BuildRankInput(session)     → RankItem[]（主框架从 session 读取数据）
RankEngine::Score(items)    → Processor 链打分（纯计算，不碰 session）
ApplyRankOutput(session)    ← 排序截断写回 session（主框架）
```

#### 过滤阶段

| Processor | 参数 | 默认值 | 建议范围 | 说明 |
|-----------|------|:------:|:--------:|------|
| **DedupFilter** | `similarity_threshold` | 0.9 | 0.8~0.95 | Jaccard 去重阈值，越低过滤越激进 |
| **QualityFilter** | `min_quality_score` | 0.0 | 0.0~0.3 | **⚠️ 新入库文档默认 quality_score=0**，设过高会误杀 |
| | `min_click_count` | 0 | 0~5 | 最低点击数，新文档建议设 0 |
| | `min_content_length` | 10 | 10~100 | 最短内容长度（字符），过滤空内容 |
| **SpamFilter** | `spam_threshold` | 0.8 | 0.6~0.9 | 垃圾分阈值，越低过滤越严格 |
| **BlacklistFilter** | `blacklist_file` | `./config/blacklist.txt` | — | 黑名单文件路径，每行一个 doc_id 或 author |

#### 后处理阶段

| Processor | 参数 | 默认值 | 建议范围 | 说明 |
|-----------|------|:------:|:--------:|------|
| **MMRReranker** | `lambda` | 0.7 | 0.5~0.9 | 相关性 vs 多样性平衡，越大越偏相关性 |
| | `top_k` | 20 | 10~50 | 最终返回结果上限 |

### Sug 搜索建议（`config/biz/sug.yaml`）

**DAG 召回阶段**：SugTrieRecallProcessor（1 路，Trie 前缀匹配）

**Rank 阶段**（`rank_config`）：

| Processor | params.features | 说明 |
|-----------|-----------------|------|
| **WeightedSumScorer** | prefix_match(0.4) + freq_norm(0.3) + freshness(0.3) | 从 KV 特征读取加权求和 |

| 参数 | 默认值 | 说明 |
|------|:------:|------|
| `sug.max_results` | 8 | 返回建议词条数 |
|`trie.rebuild_interval_sec` | 3600 | Trie 词库重建间隔（秒） |
| `trie.source_weights.user_query` | 1.2 | 用户搜索词权重最高 |
| `trie.source_weights.title` | 1.0 | 文档标题权重 |
| `trie.source_weights.tag` | 0.8 | 文档标签权重 |

### Hint 点后推荐（`config/biz/hint.yaml`）

**DAG 召回阶段**（4 路并行）：

| Processor | 召回源 | 特征字段 |
|-----------|--------|----------|
| **HintTagRecall** | 源文档标签 Jaccard > 0.1 | `tag_relevance` |
| **HintCategoryRecall** | 同分类文档 | `category_relevance` |
| **HintCooccurRecall** | 行为共现 Top-20 | `cooccur_score` |
| **HintQueryExpandRecall** | Query 前缀匹配 Top-15 | `query_relevance` |

**Rank 阶段**（`rank_config`）：

| Processor | params.features | 说明 |
|-----------|-----------------|------|
| **WeightedSumScorer** | tag_relevance(0.4) + category_relevance(0.3) + cooccur_score(0.2) + query_relevance(0.1) | 加权融合 |

### Nav 教育页（`config/biz/nav.yaml`）

**DAG 召回阶段**（3 路并行）：

| Processor | 召回源 | 特征字段 |
|-----------|--------|----------|
| **NavGlobalHotRecall** | QueryStatsStore Top-30 | `heat` |
| **NavDocTitleRecall** | 文档标题热度 | `heat` |
| **NavPresetRecall** | 静态预置词列表 | `heat`=0.5 |

**Rank 阶段**（`rank_config`）：WeightedSumScorer 读取 `heat` 特征排序

| 参数 | 默认值 | 说明 |
|------|:------:|------|
| `nav.max_results` | 6 | 展示热词条数 |

### 框架配置（`config/framework.yaml`）

| 参数 | 默认值 | 说明 |
|------|:------:|------|
| `server.port` | 8080 | HTTP 服务端口 |
| `server.worker_threads` | 4 | 工作线程数，建议 = CPU 核数 |
| `server.request_timeout_ms` | 200 | 请求超时（ms） |
| `embedding.provider` | `"onnx"` | 向量模型：`onnx`（真实 embedding）/ `pseudo`（伪向量降级） |
| `embedding.dim` | 768 | 向量维度，须与 `search.yaml` 中 `embedding_dim` 一致 |
| `pipeline.final_result_count` | 20 | 搜索结果页默认条数 |
| `background.auto_train.interval_hours` | 24 | 自动训练间隔（小时） |
| `background.auto_train.min_events` | 500 | 触发提前训练的事件数阈值 |
| `background.auto_index_rebuild.interval_hours` | 12 | 索引自动重建间隔（小时） |
| `background.sug_trie_rebuild.interval_sec` | 3600 | Sug Trie 重建间隔（秒） |

> **调参建议**：小规模（<1000 篇）保持默认即可。大规模场景重点调 `max_recall`（扩大召回量）、`BM25RankProcessor.weight`（核心排序信号）和 `MMR.lambda`（多样性）。`min_quality_score` 在文档没有预设质量分时建议保持 `0.0`，避免误过滤。Rank Processor 在 `rank_config.rank.processors` 和 `rank_config.rerank.processors` 中配置。

---

## 📜 开源协议

[Apache License 2.0](LICENSE) — 可自由使用、修改、分发，需保留版权声明。

---

<div align="center">

**如果这个项目对你有帮助，欢迎 ⭐ Star！**

[Report Bug](https://github.com/VanSherry/MiniSearchRec/issues) · [Request Feature](https://github.com/VanSherry/MiniSearchRec/issues)

</div>
