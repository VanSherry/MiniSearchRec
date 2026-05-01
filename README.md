# MiniSearchRec

> **Mini Search Recommendation Engine** — C++ 实现的完整搜索推荐引擎，参考工业级搜索推荐架构设计

[![License](https://img.shields.io/badge/license-MIT-blue)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()
[![CMake](https://img.shields.io/badge/build-CMake-green)]()
[![Status](https://img.shields.io/badge/status-可运行-brightgreen)]()
[![Demo](https://img.shields.io/badge/🌐_在线演示-GitHub_Pages-0969da)](https://vansherry.github.io/MiniSearchRec/)

> 🌐 **[在线演示 · 项目架构可视化](https://vansherry.github.io/MiniSearchRec/)** — 交互式架构图、Pipeline 全链路、训练闭环可视化

---

## 项目简介

MiniSearchRec 是一个**完整可运行**的 C++17 搜索推荐引擎，实现了工业级搜索系统的完整链路：

**召回 → 粗排 → 精排 → 过滤 → 重排 → 返回**

参考 **X (Twitter) 开源推荐算法** 和工业级搜索推荐系统架构设计，所有核心模块均已实现并通过测试。

包含完整的 **LightGBM 训练闭环**：在线行为上报 → 事件落盘 → 离线样本导出 → LambdaRank 训练 → 模型热加载。

### 已实现功能

| 模块 | 功能 | 状态 |
|------|------|------|
| 倒排索引召回 | BM25 + 多字段权重（title×3, tag×2, content×1） | ✅ 运行 |
| 用户历史召回 | 基于点击/点赞历史的个性化召回 | ✅ 运行 |
| 热门内容召回 | log(click)×0.6 + log(like)×0.4 热度排序 | ✅ 运行 |
| 向量召回 | Faiss HNSW（无 Faiss 时自动降级暴力余弦搜索） | ✅ 运行（可选 Faiss） |
| RRF 多路融合 | 倒数排名融合 / 加权平均 | ✅ 运行 |
| BM25 粗排 | k1=1.5, b=0.75，tanh 归一化 | ✅ 运行 |
| 质量分粗排 | click×0.3 + like×0.4 + quality×0.3 | ✅ 运行 |
| 新鲜度粗排 | 指数衰减 exp(-decay_rate × age_days)，连续平滑衰减 | ✅ 运行 |
| LightGBM 精排 | LambdaRank LTR，**10 维特征**（含标签匹配/类别匹配），有模型走真实推理，无模型走内置规则树 | ✅ 运行（可选 LightGBM） |
| MMR 多样性重排 | λ=0.7，**支持中文**字符级 Jaccard 相似度（UTF-8 逐字切分） | ✅ 运行 |
| 去重过滤 | 标题字符级 Jaccard 相似度 ≥0.9 | ✅ 运行 |
| 质量过滤 | quality_score / click_count / content_length | ✅ 运行 |
| Spam 过滤 | 垃圾内容检测，score 阈值可配置 | ✅ 运行 |
| 黑名单过滤 | 文件驱动黑名单，支持热更新 | ✅ 运行 |
| 用户画像管理 | proto 序列化持久化，**真正 EMA 增量**兴趣更新（不清空旧权重） | ✅ 运行 |
| 用户事件处理 | click/like/share/dwell → 画像更新 + 落盘 SQLite | ✅ 运行 |
| 训练样本导出 | dump_train_data：事件库 → LTR 格式，支持冷启动 | ✅ 运行 |
| LambdaRank 训练 | train_rank_model.py，支持增量训练 + 版本管理 | ✅ 运行 |
| LRU 缓存 | 搜索结果本地缓存，proto 序列化 | ✅ 运行 |
| Query 理解 | 分词/归一化/停用词/同义词扩展/类别推断 | ✅ 运行 |
| A/B 实验框架 | 一致性 Hash 分桶，**已接入 Pipeline**（search_handler 按 uid 分配实验组） | ✅ 运行 |

---

## 系统架构

```
HTTP 请求
    │
    ▼
┌─────────────────────────────────────────────────────┐
│  接入层 (Service Layer)                              │
│  HttpServer  ←  SearchHandler / DocHandler           │
│                  EventHandler（行为上报+落盘 SQLite）  │
└──────────────────────┬──────────────────────────────┘
                       │
    ┌──────────────────▼──────────────────────┐
    │  请求上下文 Session                      │
    │  trace_id / request / qp_info           │
    │  recall_results / final_results         │
    └──────────────────┬──────────────────────┘
                       │
    ┌──────────────────▼──────────────────────┐
    │  Query 理解层                            │
    │  归一化 → 分词 → 同义词扩展 → IDF 预计算 │
    └──────────────────┬──────────────────────┘
                       │
    ┌──────────────────▼──────────────────────┐
    │  Pipeline 编排层（多路召回并行）                │
    │  召回(≤1500, std::async并行) → 粗排(Top500)   │
    │           → LightGBM精排(Top100)              │
    │           → 过滤 → MMR重排(Top20)             │
    └──────────────────┬──────────────────────┘
                       │
    ┌──────────────────▼──────────────────────┐
    │  索引与存储层                            │
    │  InvertedIndex（内存+磁盘）              │
    │  VectorIndex（Faiss HNSW / 暴力降级）    │
    │  DocStore（SQLite docs.db）              │
    │  EventStore（SQLite events.db）          │
    └─────────────────────────────────────────┘

离线训练闭环
    │
    ├── events.db（行为日志）
    │       ↓ dump_train_data
    ├── data/train.txt（LTR 样本）
    │       ↓ train_rank_model.py
    └── models/rank_model.txt（LightGBM 模型）
            ↓ 服务加载
        LGBMScorerProcessor
```

---

## 快速开始

### 1. 依赖安装

**macOS（推荐）：**

```bash
brew install cmake yaml-cpp protobuf jsoncpp cpp-httplib spdlog sqlite3

# 可选：向量召回
brew install faiss

# 可选：LightGBM 精排（C++ 在线推理）
brew install lightgbm libomp

# Python 训练脚本依赖
pip3 install lightgbm numpy
```

**Ubuntu / Debian：**

```bash
sudo apt-get install -y \
    build-essential cmake git \
    libprotobuf-dev protobuf-compiler \
    libyaml-cpp-dev libsqlite3-dev \
    pkg-config libjsoncpp-dev libspdlog-dev

# cpp-httplib（header-only）
git clone https://github.com/yhirose/cpp-httplib.git /tmp/cpp-httplib
sudo cp /tmp/cpp-httplib/httplib.h /usr/local/include/

# Python 训练脚本依赖
pip3 install lightgbm numpy
```

### 2. 编译

```bash
git clone https://github.com/VanSherry/MiniSearchRec.git
cd MiniSearchRec

# 编译主程序
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TOOLS=ON
cmake --build build -j4
```

编译产物：
- `build/minisearchrec` — 主服务
- `build/build_index` — 索引构建工具
- `build/dump_train_data` — 离线样本导出工具

### 3. 初始化索引

```bash
# 从 data/sample_docs.json 构建倒排索引和文档库
./build/minisearchrec --config ./config --build-index
```

输出：
```
[info] Building index from: ./data/sample_docs.json
[IndexBuilder] Building index for 5 documents...
[InvertedIndex] Saved to ./index/inverted.idx, terms=182, docs=5
[info] Index rebuild complete. Exiting.
```

### 4. 启动服务

```bash
./build/minisearchrec --config ./config
```

输出：
```
[info] Indexes loaded from disk, doc_count=5
[info] Starting HTTP server on 0.0.0.0:8080
[info] Server ready. Press Ctrl+C to stop.
```

### 5. 快速测试

```bash
# 搜索
curl -X POST http://localhost:8080/api/v1/search \
  -H "Content-Type: application/json" \
  -d '{"query": "深度学习", "uid": "user_001", "page": 1, "page_size": 5}'

# 上报点击行为
curl -X POST http://localhost:8080/api/v1/event/click \
  -H "Content-Type: application/json" \
  -d '{"uid": "user_001", "doc_id": "doc_001", "query": "深度学习", "result_pos": 0}'
```

---

## LightGBM 训练闭环

> 不需要任何预先标注数据。用户行为（点击/点赞/停留）即标签，系统自动完成从数据收集到模型更新的完整闭环。

### 架构说明

```
在线服务
  ↓ 用户行为上报（POST /api/v1/event/click）
  ↓ 写入 data/events.db（含特征快照：bm25/quality/freshness/...）
  ↓ 实时更新用户画像（UserProfile）

离线训练（定时任务）
  ↓ dump_train_data：events.db → data/train.txt（LTR 格式）
  ↓ train_rank_model.py：LambdaRank 训练 → models/rank_model.txt
  ↓ 重启服务或热更新，LGBMScorerProcessor 加载新模型
```

### 冷启动（首次，无用户数据）

```bash
# 基于 docs.db 中文档质量合成正负样本
./build/dump_train_data --cold-start \
  --docs-db ./data/docs.db \
  --output ./data/train.txt

# 训练模型
PYTHONPATH=~/.local/lib/python3.9/site-packages \
python3 scripts/train_rank_model.py \
  --input ./data/train.txt \
  --output ./models/rank_model.txt \
  --num-trees 50
```

### 有真实数据后（增量训练）

```bash
# 导出最近 7 天的行为数据
./build/dump_train_data \
  --min-ts $(date -d '7 days ago' +%s 2>/dev/null || date -v-7d +%s) \
  --output ./data/train_incremental.txt

# 在已有模型基础上增量训练
python3 scripts/train_rank_model.py \
  --input ./data/train_incremental.txt \
  --output ./models/rank_model.txt \
  --incremental \
  --num-trees 50
```

### label 映射规则

| 事件类型 | label | 说明 |
|--------|-------|------|
| `like` | 3 | 强正样本 |
| `click` / `share` | 2 | 正样本 |
| `dwell`（停留 ≥15s） | 1 | 弱正样本 |
| `dwell`（停留 <15s） | 1 | 弱正 |
| `dismiss` | 0 | 负样本 |

### 特征说明（与 `lgbm_ranker.cpp` 完全对齐）

| 索引 | 特征名 | 来源 |
|------|--------|------|
| 1 | `query_len` | query 分词数量（tanh/5 归一化） |
| 2 | `bm25_score` | 粗排 BM25 分，上报时携带 |
| 3 | `quality_score` | 质量分 |
| 4 | `freshness_score` | 时效性分（指数衰减） |
| 5 | `log_click` | log(click+1) tanh/5 归一化 |
| 6 | `log_like` | log(like+1) tanh/5 归一化 |
| 7 | `title_len` | 标题长度（tanh/30 归一化） |
| 8 | `tag_match_count` | 标题/类别命中 query 词数（tanh/3 归一化） |
| 9 | `category_match` | 用户兴趣类别与文章类别匹配权重（[0,1]） |
| 10 | `recall_source_id` | 召回来源编号（inverted=0/vector=1/hot=2/history=3） |

### 降级机制

| 情况 | 行为 |
|------|------|
| 已安装 LightGBM + 模型文件存在 | 调用真实 C API 推理 |
| 模型文件不存在 | 降级到内置规则树（3 棵决策树 ensemble） |
| 未安装 LightGBM（编译时） | 仅内置规则树，无需安装任何依赖 |

---

## API 接口

### 健康检查

```
GET /health
```

```json
{"status": "ok", "doc_count": 5, "term_count": 182}
```

---

### 搜索

```
POST /api/v1/search
```

**请求体：**
```json
{
  "query": "深度学习",
  "uid": "user_12345",
  "page": 1,
  "page_size": 20,
  "business_type": "article"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `query` | string | ✅ | 搜索词，最长 200 字符 |
| `uid` | string | 否 | 用户 ID，用于个性化召回 |
| `page` | int | 否 | 页码，默认 1 |
| `page_size` | int | 否 | 每页数量，默认 20，最大 100 |
| `business_type` | string | 否 | 业务类型，默认 "default" |

**响应体：**
```json
{
  "ret": 0,
  "err_msg": "",
  "trace_id": "MSR-1714492800000-1234",
  "total": 2,
  "cost_ms": 1,
  "page": 1,
  "page_size": 20,
  "results": [
    {
      "doc_id": "doc_001",
      "title": "深度学习从零开始",
      "snippet": "深度学习是机器学习的一个重要分支...",
      "score": 1.842,
      "recall_source": "inverted_index",
      "author": "张三",
      "publish_time": 1714492800,
      "click_count": 1250,
      "like_count": 380,
      "debug_scores": {
        "bm25": 0.967,
        "quality": 0.875,
        "freshness": 0.0,
        "lgbm": 0.812
      }
    }
  ]
}
```

---

### 用户行为上报

```
POST /api/v1/event/click
POST /api/v1/event/like
POST /api/v1/event/share
POST /api/v1/event/dismiss
POST /api/v1/event/dwell
```

**请求体：**
```json
{
  "uid": "user_12345",
  "doc_id": "doc_001",
  "query": "深度学习",
  "result_pos": 0,
  "duration_ms": 30000,
  "bm25_score": 0.967,
  "quality_score": 0.875,
  "freshness_score": 0.0,
  "coarse_score": 1.842,
  "fine_score": 0.812
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `uid` | ✅ | 用户 ID |
| `doc_id` | ✅ | 文档 ID |
| `query` | 否 | 触发事件的搜索词 |
| `result_pos` | 否 | 结果排名位置（0-based） |
| `duration_ms` | 否 | 停留时长（dwell 事件专用） |
| `bm25_score`~`fine_score` | 否 | 展示时的特征快照，用于训练样本构建 |

> **建议**：前端在搜索结果展示时缓存 `debug_scores`，点击时一并上报，使训练样本的特征值与打分时完全一致。

**响应：**
```json
{"ret": 0, "err_msg": ""}
```

---

### 添加文档

```
POST /api/v1/doc/add
```

```json
{
  "doc_id": "doc_101",
  "title": "向量数据库原理",
  "content": "向量数据库通过高维向量的近似最近邻检索实现语义搜索...",
  "author": "作者名",
  "category": "tech",
  "tags": ["向量", "数据库", "搜索"],
  "quality_score": 0.88,
  "click_count": 500,
  "like_count": 120,
  "publish_time": 1714838400
}
```

添加后文档立即对搜索可见（实时更新内存索引）。

---

### 更新文档

```
PUT /api/v1/doc/update
```

请求体同 `doc/add`，按 `doc_id` 覆盖更新。

---

### 删除文档

```
DELETE /api/v1/doc/delete?doc_id=doc_101
```

---

## 项目结构

```
MiniSearchRec/
├── CMakeLists.txt            # 构建配置（支持 Faiss/LightGBM 条件编译）
├── config/
│   ├── global.yaml           # 服务器/索引/日志/缓存配置
│   ├── recall.yaml           # 召回策略配置
│   ├── rank.yaml             # 粗排+LightGBM精排+后处理配置
│   └── filter.yaml           # 过滤策略配置
├── proto/
│   ├── search.proto          # SearchRequest / SearchResponse
│   ├── doc.proto             # Document
│   ├── user.proto            # UserProfile / UserEvent / EventType
│   └── event.proto           # 文档管理请求
├── src/
│   ├── core/
│   │   ├── app_context.cpp   # 全局单例：索引/DocStore 依赖注入
│   │   ├── pipeline.cpp      # 搜索漏斗编排（召回→排序→过滤→重排）
│   │   ├── session.h         # 请求上下文（QPInfo/DocCandidate）
│   │   ├── processor.h       # 处理器基类 + ProcessorFactory
│   │   ├── factory.cpp       # 内置处理器注册
│   │   └── config_manager.cpp
│   ├── recall/
│   │   ├── inverted_recall.cpp     # 倒排索引召回
│   │   ├── user_history_recall.cpp # 用户历史个性化召回
│   │   ├── hot_content_recall.cpp  # 热门内容召回
│   │   ├── vector_recall.cpp       # 向量语义召回（Faiss HNSW / 暴力降级）
│   │   └── recall_fusion.cpp       # RRF 多路召回融合
│   ├── index/
│   │   ├── inverted_index.cpp  # 倒排索引（含磁盘持久化）
│   │   ├── vector_index.cpp    # Faiss HNSW / 暴力余弦搜索降级
│   │   ├── doc_store.cpp       # SQLite 文档存储
│   │   └── index_builder.cpp   # 从 JSON 批量构建索引
│   ├── rank/
│   │   ├── bm25_scorer.cpp     # BM25 打分（k1=1.5, b=0.75）
│   │   ├── quality_scorer.cpp  # 质量分（click/like/quality 加权）
│   │   ├── freshness_scorer.cpp# 新鲜度时效打分
│   │   ├── lgbm_ranker.cpp     # LightGBM LTR（C API / 内置规则树降级）
│   │   └── mmr_reranker.cpp    # MMR 多样性重排（λ=0.7）
│   ├── filter/
│   │   ├── dedup_filter.cpp    # 标题相似度去重
│   │   ├── quality_filter.cpp  # 质量/内容长度过滤
│   │   ├── spam_filter.cpp     # 垃圾内容检测
│   │   └── blacklist_filter.cpp# 黑名单过滤
│   ├── query/
│   │   ├── query_parser.cpp    # 分词/归一化/类别推断
│   │   ├── query_normalizer.cpp# 全角转半角/去噪/停用词
│   │   └── query_expander.cpp  # 同义词/缩写扩展
│   ├── user/
│   │   ├── user_profile.cpp    # 用户画像 proto 持久化
│   │   ├── user_event_handler.cpp # 行为事件→画像更新
│   │   └── user_interest_updater.cpp # 兴趣权重重计算+时间衰减
│   ├── cache/
│   │   ├── lru_cache.h         # 模板 LRU 缓存
│   │   ├── cache_manager.cpp   # 搜索结果缓存管理
│   │   └── redis_client.cpp    # Redis 客户端（含内存 Mock）
│   ├── ab/
│   │   └── ab_test.cpp         # A/B 实验一致性 Hash 分桶
│   ├── service/
│   │   ├── http_server.cpp     # cpp-httplib HTTP 服务器
│   │   ├── search_handler.cpp  # 搜索接口（Pipeline 单例+缓存）
│   │   ├── doc_handler.cpp     # 文档增删改接口
│   │   └── event_handler.cpp   # 行为上报（落盘 SQLite + 更新画像）
│   └── utils/
│       ├── string_utils.cpp    # UTF-8 安全分词/截断/大小写
│       ├── vector_utils.cpp    # 向量运算（余弦相似度/L2归一化等）
│       ├── logger.cpp          # spdlog 封装（无 spdlog 时降级 stdout）
│       └── hash_utils.cpp      # 哈希工具
├── data/
│   ├── sample_docs.json        # 示例文档（可替换）
│   ├── docs.db                 # SQLite 文档库（运行时生成）
│   ├── events.db               # SQLite 行为日志（运行时生成）
│   └── train.txt               # LTR 训练样本（dump_train_data 生成）
├── index/                      # 倒排索引文件（运行时生成）
├── models/
│   ├── rank_model.txt          # LightGBM 模型（训练后生成）
│   └── rank_model.feat_names.txt # 特征名称
├── scripts/
│   └── train_rank_model.py     # LambdaRank 训练脚本
├── static/                     # 架构图 HTML 展示页
├── tests/                      # 单元测试（GTest）
└── tools/
    ├── build_index.cpp         # 独立索引构建工具
    └── dump_train_data.cpp     # 离线样本导出工具（含冷启动）
```

---

## 配置说明

所有业务逻辑由 YAML 配置驱动，**修改配置无需重新编译**。

### config/global.yaml

```yaml
server:
  port: 8080
  host: "0.0.0.0"
  worker_threads: 4

index:
  data_dir: "./data/"
  index_dir: "./index/"
  rebuild_on_start: false   # true 则每次启动重建索引

logging:
  level: "info"             # trace/debug/info/warn/error
  file: "./logs/minisearchrec.log"

cache:
  enable: true
  local_capacity: 100       # LRU 缓存最大条目数
```

### config/recall.yaml

```yaml
recall_stages:
  - name: "InvertedRecallProcessor"
    enable: true
    max_recall: 1000
  - name: "UserHistoryRecallProcessor"
    enable: true
    max_recall: 200
  - name: "HotContentRecallProcessor"
    enable: true
    max_recall: 100
    params:
      refresh_interval_sec: 300  # 热榜缓存刷新间隔（秒），避免全表扫描
  # - name: "VectorRecallProcessor"   # 需安装 Faiss + 生成 query embedding
```

### config/rank.yaml

```yaml
coarse_rank_stages:
  - name: "BM25ScorerProcessor"
    weight: 0.6
  - name: "QualityScorerProcessor"
    weight: 0.2
  - name: "FreshnessScorerProcessor"
    weight: 0.2
    params:
      max_age_days: 365
      decay_rate: 0.01   # 指数衰减 exp(-decay_rate * age_days)，连续平滑

fine_rank_stages:
  - name: "LGBMScorerProcessor"
    weight: 0.8
    model_path: "./models/rank_model.txt"   # 不存在时自动降级内置规则树

postprocess_stages:
  - name: "MMRRerankProcessor"
    params:
      lambda: 0.7
      top_k: 20
```

---

## 数据导入

### 方式一：JSON 文件批量导入

修改 `data/sample_docs.json`，然后重建索引：

```bash
./build/minisearchrec --config ./config --build-index
```

JSON 格式：
```json
[
  {
    "doc_id": "doc_001",
    "title": "文档标题",
    "content": "文档正文内容",
    "author": "作者",
    "publish_time": 1714492800,
    "category": "tech",
    "tags": ["标签1", "标签2"],
    "quality_score": 0.92,
    "click_count": 1250,
    "like_count": 380
  }
]
```

### 方式二：HTTP API 实时导入

```bash
curl -X POST http://localhost:8080/api/v1/doc/add \
  -H "Content-Type: application/json" \
  -d '{
    "doc_id": "doc_new",
    "title": "新文档标题",
    "content": "文档内容...",
    "category": "tech",
    "quality_score": 0.85,
    "click_count": 0,
    "like_count": 0,
    "publish_time": 1714838400
  }'
```

实时导入后立即对搜索可见，无需重启。

---

## 技术选型

| 组件 | 实际使用 | 说明 |
|------|----------|------|
| 编程语言 | C++17 | 高性能，适合搜索推荐系统 |
| HTTP 框架 | cpp-httplib 0.43 | header-only，无外部依赖 |
| 配置管理 | yaml-cpp 0.9 | YAML 配置驱动 |
| 序列化 | Protocol Buffers 7.34 | 请求/响应/用户画像 |
| 文档存储 | SQLite3 | 文档元数据 + 行为事件日志 |
| 日志 | spdlog 1.17 | 结构化日志，无 spdlog 时降级 stdout |
| JSON 解析 | jsoncpp 1.9 | HTTP 请求/响应处理 |
| 向量索引 | Faiss（可选） | HNSW ANN，无则降级余弦暴力搜索 |
| ML 排序 | LightGBM（可选） | LambdaRank LTR，无则内置规则树 |
| 训练框架 | LightGBM Python | LambdaRank NDCG 优化 |
| 缓存 | LRU 内存缓存 | 搜索结果缓存，可接 Redis |

---

## 工业对齐

| 本项目 | 工业实现参考 | X (Twitter) |
|--------|------------|-------------|
| `InvertedIndex` | 标准倒排引擎 | Earlybird |
| `VectorIndex` (Faiss HNSW) | HNSW 向量检索 | SimClusters ANN |
| `UserHistoryRecallProcessor`（含 query 相关性过滤） | 个性化召回 | UTEG |
| `HotContentRecallProcessor`（热榜定时缓存） | 热门内容推荐 | Trending |
| `BM25ScorerProcessor` | L1/L2 粗排打分 | Earlybird Light Ranker |
| `LGBMScorerProcessor`（10 维特征） | GBDT 精排模型 | Heavy Ranker |
| `dump_train_data` + `train_rank_model.py` | 离线训练闭环 | ML Platform |
| `MMRRerankProcessor`（支持中文） | 多样性重排 | Diversity Ranker |
| `ABTestManager`（已接入 Pipeline） | 实验分流 | FeatureSwitch |
| `Session` | 请求上下文 | HomeMixer Context |
| `ProcessorFactory` | 算子注册/热插拔 | Component Registry |
| `AppContext` | 全局服务注册表 | Service Container |
| `EventHandler`（事件落盘） | 用户行为流水线 | Unified User Actions |
| `CacheManager`（含 uid 的 cache key） | 双层缓存 | Timeline Cache |
| `std::async` 并行召回 | 并发 candidate source | BatchTask |
| EMA 增量兴趣更新 | 实时用户画像 | Real-time Feature Store |

---

## 本轮优化摘要（v1.1）

| 问题 | 修复方案 |
|------|----------|
| `freshness_scorer` 硬编码阶梯，`decay_rate` 从未使用 | 改为 `exp(-decay_rate × age_days)` 连续指数衰减 |
| 用户兴趣更新全量重算（先 `clear()` 再重建），声称 EMA 实际批量 | 去掉 `clear()`，改为真正 EMA：`new = α × signal + (1-α) × old` |
| 历史召回无 query 相关性过滤，"量子力学"会召回"美食"文章 | 添加 query terms + inferred_category 相关性过滤 |
| 多路召回去重 O(N²)（线性遍历 `recall_results`） | 所有召回处理器统一改为 `unordered_set` O(1) 查找 |
| `HotContent` 每次请求全表扫描 O(N) | 增加热榜快照缓存（`mutex` + 懒刷新，默认5分钟） |
| 多路召回串行 for 循环 | 改为 `std::async(std::launch::async)` 并行 + 最终合并去重 |
| LightGBM 仅 6 维特征 | 扩展至 **10 维**（+标题长度/标签命中数/类别匹配/召回来源） |
| MMR 中文相似度退化（无空格分词结果为整串） | 重写为 UTF-8 字符级分词（中文逐字，英文按词） |
| A/B 框架有代码但未接入 Pipeline | `AppContext` 注入 `ABTestManager`，`search_handler` 按 uid 实际分流 |
| Spam/Blacklist 过滤器被注释禁用 | `filter.yaml` 启用两个过滤器 |
| `build_index` 工具因 spdlog/命名空间/vtable 问题无法编译 | 修复 CMakeLists.txt 链接、补 `using namespace`、拆 `CORE_BASE_SRCS` |

---

## 命令行参数

```bash
./build/minisearchrec [options]

  --config <path>     配置目录路径（默认: ./config）
  --build-index       强制重建索引后退出
  --help              显示帮助

./build/dump_train_data [options]

  --events-db  <path>  事件数据库（默认: ./data/events.db）
  --docs-db    <path>  文档数据库（默认: ./data/docs.db）
  --output     <path>  输出 LTR 文件（默认: ./data/train.txt）
  --min-ts     <ts>    只导出 >= 此时间戳的事件
  --cold-start         生成冷启动合成样本

python3 scripts/train_rank_model.py [options]

  --input  <path>   训练数据文件（默认: ./data/train.txt）
  --output <path>   模型输出路径（默认: ./models/rank_model.txt）
  --eval   <path>   验证集（可选）
  --num-trees <n>   树数量（默认: 100）
  --leaves    <n>   叶节点数（默认: 31）
  --lr        <f>   学习率（默认: 0.05）
  --incremental     在已有模型基础上增量训练
```

---

## License

MIT License

---

## 致谢

- 参考 [X (Twitter) 开源推荐算法](https://github.com/twitter/the-algorithm)
- 参考工业级搜索推荐系统架构设计
- 使用 [cpp-httplib](https://github.com/yhirose/cpp-httplib) / [spdlog](https://github.com/gabime/spdlog) / [yaml-cpp](https://github.com/jbeder/yaml-cpp) / [LightGBM](https://github.com/microsoft/LightGBM)
