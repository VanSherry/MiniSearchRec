<div align="center">

# MiniSearchRec

**生产级 C++17 搜索推荐引擎**

*配置驱动 · 框架优先 · 零改代码扩展业务*

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=flat-square&logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.16+-064F8C.svg?style=flat-square&logo=cmake)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](LICENSE)
[![Tests](https://img.shields.io/badge/Tests-117%2F117_passed-brightgreen.svg?style=flat-square)](#测试)
[![Lines](https://img.shields.io/badge/C%2B%2B_Lines-16%2C600%2B-informational.svg?style=flat-square)](#)

<br/>

单进程内嵌 **索引构建 · 查询理解 · 多级排序 · 用户画像 · AB实验 · 在线训练**<br/>
完整展示现代搜索系统从 Query 到 Result 到 Feedback 的闭环运作原理

<br/>

[快速开始](#-快速开始) · [系统架构](#-系统架构) · [交互闭环](#-用户交互闭环) · [技术亮点](#-技术亮点) · [API 文档](#-api-接口)

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

### 🔍 搜索能力
- **4 路召回**：倒排索引 / 向量语义 / 用户历史 / 热门内容
- **3 级排序**：BM25+Quality+Freshness 粗排 → LightGBM 精排 → MMR 多样性重排
- **4 种过滤**：去重 / 质量 / 垃圾检测 / 黑名单

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
│  (垂搜)  │(下拉联想) │ (相关搜索)│(搜前引导) │   (文档·行为·管理)    │
├──────────┴──────────┴──────────┴──────────┴──────────────────────┤
│                     framework/ (框架层 · 零改扩展)                 │
│  ┌────────────┐ ┌────────────┐ ┌──────────────┐ ┌────────────┐  │
│  │  Handler   │ │  Session   │ │  Processor   │ │  Server    │  │
│  │  Manager   │ │  Factory   │ │  Pipeline    │ │  Router    │  │
│  └────────────┘ └────────────┘ └──────────────┘ └────────────┘  │
├──────────────────────────────────────────────────────────────────┤
│                     lib/ (公共算子库 · 各业务按需调用)               │
│  index/  query/  recall/  rank/  filter/  feature/               │
│  embedding/  storage/  user/                                     │
├──────────────────────────────────────────────────────────────────┤
│  scheduler/ (定时任务)  │  cache/ (多级缓存)  │  ab/ (实验框架)     │
└─────────────────────────┴─────────────────────┴──────────────────┘
```

---

## 🔄 用户交互闭环

MiniSearchRec 实现了搜索产品的 **三条完整交互链路**：

```
                        用户打开搜索框
                              │
                    ┌─────────▼─────────┐
                    │       Nav         │  GET /nav
                    │    搜前引导页      │  热词推荐（6 条）
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
          │                                                │ 点击建议词
          │                                                │
          ▼────────────────────────────────────────────────▼
                              │
                    ┌─────────▼─────────┐
                    │      Search       │  POST /search
                    │    搜索结果页      │
                    │                   │
                    │  4路召回 → 粗排(3)  │  BM25 + Quality + Freshness
                    │  → 精排(LGBM)     │  10 维特征 → 决策树/LGBM
                    │  → 过滤(4) → MMR  │  去重·质量·垃圾·黑名单·多样性
                    │                   │
                    └────┬─────────┬────┘
                         │         │
              点击文档     │         │  上报行为
                         │         └──────────────────────┐
                         │                                │
               ┌─────────▼─────────┐           ┌─────────▼─────────┐
               │       Hint        │           │    Event 上报      │
               │  相关搜索（8 条）   │           │                   │
               │ 标签匹配+行为共现   │           │  ┌─→ EventDB      │  特征快照→训练
               │ +分类热词+查询扩展  │           │  ├─→ UserProfile  │  画像更新
               └────────┬──────────┘           │  ├─→ QueryStats   │  Sug Trie 数据
                        │                      │  ├─→ DocCooccur   │  Hint 共现数据
                 点击 Hint 词条                  │  └─→ Nav History  │  Nav 个性化
                        │                      └───────────────────┘
                        │                                │
                        └──→ 再次触发 Search ←────────────┘
                                                    数据飞轮
```

> **数据飞轮**：用户每次交互产生 5 路数据写入，后台定时任务（训练/索引重建/Trie重建）不断将数据反哺到各业务，使搜索效果持续提升。

---

## 📐 搜索主流程（Template Method）

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
    ├── 3. DoSearch              ← recall_pipeline（4 路召回）
    │       ├── InvertedRecall   倒排索引 · tf×idf 打分
    │       ├── VectorRecall     向量近邻 · embedding 语义匹配
    │       ├── HotContentRecall 热榜缓存 · CAS 原子刷新
    │       └── UserHistoryRecall 用户历史 · query 相关性过滤
    │
    ├── 4. DoRank                ← rank_pipeline（粗排 · 3 打分器加权融合）
    │       ├── BM25Scorer       (w=0.6) tanh(BM25/10) 归一化
    │       ├── QualityScorer    (w=0.2) click + like + quality
    │       └── FreshnessScorer  (w=0.2) exp(-0.01 × age_days)
    │       AfterRank → sort → truncate top-500
    │
    ├── 5. DoRerank              ← rerank_pipeline（精排）
    │       └── LGBMScorer       (w=0.8) 10 维特征 → LightGBM / 内置决策树
    │       AfterRerank → sort → truncate top-100
    │
    ├── 6. DoInterpose           ← filter + postprocess pipeline
    │       ├── DedupFilter      UTF-8 字符级 Jaccard ≥ 0.9
    │       ├── QualityFilter    quality < 0.3 / snippet < 50
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
│   └── biz/                        # 各业务独立 Pipeline 配置
│       ├── search.yaml             #   召回(4路) → 粗排(3) → 精排(1) → 过滤(4) → 后处理(1)
│       ├── sug.yaml                #   Trie 前缀匹配 + 相关性排序
│       ├── hint.yaml               #   标签匹配 + 行为共现 + 查询扩展
│       └── nav.yaml                #   全局热词 + 用户历史 + 分类热词
├── src/
│   ├── framework/                  # 框架层（新增业务不碰此目录）
│   │   ├── handler/                #   BaseHandler + HandlerManager
│   │   ├── session/                #   Session 生命周期 + KV/Any 存储
│   │   ├── processor/              #   ProcessorPipeline（YAML → 反射 → 按序执行）
│   │   ├── server/                 #   统一请求路由
│   │   ├── config/                 #   ConfigManager
│   │   ├── app_context.h           #   全局 DI 容器（线程安全）
│   │   └── class_register.h        #   反射注册宏
│   ├── biz/                        # 业务实现层
│   │   ├── search/                 #   全文搜索（SearchSession + DocCandidate + 4 种 Base Processor 适配器）
│   │   ├── sug/                    #   搜索建议（Trie 双 Buffer + RebuildTrie 飞轮）
│   │   ├── hint/                   #   点后推荐（DocCooccurStore 行为共现）
│   │   ├── nav/                    #   搜前引导（热词召回 + 冷启动预置词兜底）
│   │   ├── doc/                    #   文档 CRUD API
│   │   └── event/                  #   事件接入（5 路数据写入）
│   ├── lib/                        # 公共算子库
│   │   ├── index/                  #   InvertedIndex(shared_mutex) + VectorIndex(Faiss/暴力) + DocStore(SQLite)
│   │   ├── query/                  #   QueryParser → Normalizer → Expander → Embedding
│   │   ├── recall/                 #   4 路召回 + RRF/WeightedAvg 融合
│   │   ├── rank/scorer/            #   BM25 / Quality / Freshness / LightGBM(双Buffer热更新)
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
├── tests/                          # 集成测试（117 cases）
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

# 编译
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)

# 运行测试
ln -sf ../config config && ln -sf ../models models && ln -sf ../data data
./test_all    # 预期：PASS=117, FAIL=0

# 启动服务
./minisearchrec --config ./config
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
| `/api/v1/hint` | GET | 相关搜索（点后推） | `doc_id`, `query`（可选） |
| `/api/v1/nav` | GET | 搜前引导（热词推荐） | `uid`（可选，个性化） |

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
# Step 1：编写 Handler（继承 BaseHandler，覆写钩子函数）
vim src/biz/xxx/xxx_handler.cpp
# 文件末尾：REGISTER_MSR_HANDLER(XxxBizHandler);

# Step 2：编写 Processor（继承 ProcessorInterface，实现 Init + Process）
vim src/lib/xxx/xxx_processor.cpp
# 文件末尾：REGISTER_MSR_PROCESSOR(XxxProcessor);

# Step 3：添加配置
vim config/biz/xxx.yaml           # Pipeline 各阶段 Processor 配置
vim config/framework.yaml         # businesses[] 中加一条路由

# 重新编译启动 —— 完成！
```

---

## 🎨 设计模式一览

| 模式 | 应用 | 效果 |
|------|------|------|
| **Template Method** | `BaseHandler::Search()` 8 阶段骨架 | 业务只需覆写钩子 |
| **Strategy** | EmbeddingProvider / 各 Processor | 配置一键切换实现 |
| **Pipeline** | `ProcessorPipeline` 链式执行 | YAML 驱动编排 |
| **Registry + Reflection** | `REGISTER_MSR_*` 宏 | 配置驱动零代码注册 |
| **Double Buffer** | LGBMScorer / AppContext::SwapIndexes | 无锁热更新 |
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

## 📜 开源协议

[MIT License](LICENSE) — 可自由使用、修改、分发。

---

<div align="center">

**如果这个项目对你有帮助，欢迎 ⭐ Star！**

[Report Bug](https://github.com/VanSherry/MiniSearchRec/issues) · [Request Feature](https://github.com/VanSherry/MiniSearchRec/issues)

</div>
