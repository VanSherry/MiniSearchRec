# MiniSearchRec

一个生产级的 C++17 搜索推荐引擎，采用**框架驱动、配置优先**的架构设计，新增业务零改框架代码。

单进程内嵌索引构建、查询理解、多级排序、用户画像，完整展示现代搜索系统的内部运作原理。

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                      gateway/ (HTTP 网关)                    │
├────────────┬────────────┬────────────┬──────────────────────┤
│   search   │    sug     │    hint    │        nav           │
│   (垂搜)   │ (搜索建议)  │  (点后推)  │    (搜前引导)         │
├────────────┴────────────┴────────────┴──────────────────────┤
│                     framework/ (框架层)                      │
│  ┌──────────┐ ┌──────────┐ ┌────────────┐ ┌──────────────┐ │
│  │ Handler  │ │ Session  │ │ Processor  │ │   Server     │ │
│  │ Manager  │ │ Factory  │ │ Pipeline   │ │   Router     │ │
│  └──────────┘ └──────────┘ └────────────┘ └──────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    lib/ (公共算子库)                          │
│  index/  query/  recall/  rank/  filter/  feature/          │
│  embedding/  storage/  user/                                │
├─────────────────────────────────────────────────────────────┤
│  scheduler/  │  cache/  │  ab/  │  utils/                   │
└──────────────┴──────────┴───────┴───────────────────────────┘
```

## 核心设计原则

| 原则 | 实现方式 |
|------|---------|
| **配置优先** | 所有业务路由、Processor 流水线、定时任务均由 YAML 驱动，新增业务不改代码 |
| **框架驱动** | Template Method 模式：`BaseHandler` → `ExtraInit / DoSearch / DoRank / SetResponse` 钩子 |
| **反射注册** | `REGISTER_MSR_HANDLER` / `REGISTER_MSR_PROCESSOR` 宏实现自动注册 |
| **三层分离** | `framework/`（骨架）→ `biz/`（业务逻辑）→ `lib/`（可复用算子） |

## 项目结构

```
MiniSearchRec/
├── config/
│   ├── framework.yaml          # 框架配置（服务器/Embedding/调度器/Handler路由表）
│   └── biz/                    # 各业务独立 Processor Pipeline 配置
│       ├── search.yaml         # 召回 → 粗排 → 精排 → 过滤 → 后处理
│       ├── sug.yaml            # 搜索建议
│       ├── hint.yaml           # 点后推
│       └── nav.yaml            # 搜前引导
├── src/
│   ├── framework/              # 框架层（新增业务不碰此目录）
│   │   ├── handler/            # BaseHandler + HandlerManager（配置驱动注册）
│   │   ├── session/            # Session 生命周期 + SessionFactory
│   │   ├── processor/          # ProcessorPipeline（YAML → 反射创建 → 按序执行）
│   │   ├── server/             # 统一请求路由分发
│   │   ├── config/             # ConfigManager（框架配置管理）
│   │   ├── app_context.h       # 全局依赖注入容器
│   │   └── class_register.h    # 反射注册宏
│   ├── biz/                    # 业务实现层
│   │   ├── search/             # 全文搜索（多路召回 + 多级排序）
│   │   ├── sug/                # 搜索建议（Trie 召回 + 相关性打分）
│   │   ├── hint/               # 点后推荐（行为共现 + 协同过滤）
│   │   ├── nav/                # 搜前引导（热词 + 个性化推荐）
│   │   ├── doc/                # 文档增删改查 API
│   │   └── event/              # 用户行为事件接入
│   ├── lib/                    # 公共算子库（各业务按需调用）
│   │   ├── index/              # 倒排索引 + 向量索引 + 文档存储
│   │   ├── query/              # 查询理解（分词/归一化/同义词扩展）
│   │   ├── recall/             # 多路召回（倒排/向量/用户历史/热门内容/RRF融合）
│   │   ├── rank/               # 排序算子
│   │   │   ├── scorer/         # BM25 / 质量分 / 时效性 / LightGBM
│   │   │   └── reranker/       # MMR 多样性重排
│   │   ├── filter/             # 过滤算子（去重/质量/Spam/黑名单）
│   │   ├── feature/            # 特征提取（Query/User/Doc 特征）
│   │   ├── embedding/          # 内置 ONNX 向量模型 (bge-base-zh-v1.5)
│   │   ├── storage/            # 离线统计存储（搜索词热度/行为共现）
│   │   └── user/               # 用户画像（Profile/Event/兴趣更新）
│   ├── scheduler/              # 后台定时任务调度器
│   │   ├── scheduler.h/cpp     # 事件循环 + InitFromConfig
│   │   └── task/               # 自动训练 / 索引重建 / Trie 重建
│   ├── gateway/                # HTTP 网关（cpp-httplib）
│   ├── cache/                  # 多级缓存（本地 LRU + Redis）
│   ├── ab/                     # A/B 实验框架
│   └── utils/                  # 工具（日志/字符串/哈希/向量运算）
├── models/
│   └── bge-base-zh/            # 内置 ONNX Embedding 模型（99MB）
├── proto/                      # Protobuf 协议定义
├── deps/                       # 第三方头文件（httplib）
├── tests/                      # 集成测试套件
└── data/                       # 示例文档数据
```

**153 个源文件 · 16,600+ 行 C++17 代码**

## 请求处理流程

```
HTTP 请求
    │
    ▼
Server::Search(business_type, request)
    │
    ▼
HandlerManager::GetHandler(business_type)   ← 配置驱动路由
    │
    ▼
BaseHandler::Search(session)                ← Template Method 主流程
    │
    ├── InitSession          初始化 Session
    ├── CanSearch             准入检查 + 干预词检查
    ├── PreSearch             查询理解 + AB 实验染色
    ├── DoSearch              ← recall_pipeline（从 YAML 加载）
    ├── DoRank                ← rank_pipeline（粗排）
    ├── DoRerank              ← rerank_pipeline（精排）
    ├── DoInterpose           干预处理（封禁/仅出/提权/降权）
    ├── SetResponse           分页 + JSON 序列化
    └── ReportFinal           指标上报 + 日志（ScopeGuard 保证执行）
```

## 新增业务（零改框架代码）

```bash
# 1. 编写 Handler
src/biz/xxx/xxx_handler.h      # 继承 BaseHandler，覆写 ExtraInit/DoSearch/DoRank/...
src/biz/xxx/xxx_handler.cpp     # 文件末尾加 REGISTER_MSR_HANDLER(XxxBizHandler)

# 2. 添加配置
config/biz/xxx.yaml             # 定义 Processor Pipeline 各阶段
config/framework.yaml           # businesses[] 中加一条

# 3. 重启服务 —— 完成
```

## 内置 Embedding 模型

项目自带 **bge-base-zh-v1.5**（INT8 量化版，99MB），开箱即用的中文语义搜索：

- 768 维向量输出
- WordPiece 分词器（21,128 词表）
- ONNX Runtime 推理（CPU 单条约 15ms）
- 三级降级策略：ONNX Runtime → TokenID 哈希投影 → 词袋伪向量

## 主要配置说明

所有配置集中在 `config/framework.yaml`：

```yaml
server:
  host: "0.0.0.0"
  port: 8080

embedding:
  provider: "onnx"                              # onnx / pseudo
  model_path: "models/bge-base-zh/model.onnx"
  dim: 768

businesses:                                     # Handler 路由表（配置驱动）
  - business_type: "search"
    handler_name: "SearchBizHandler"
    session_name: "SearchSession"

background:                                     # 后台任务（配置驱动）
  auto_train:
    enable: true
    interval_hours: 24
  auto_index_rebuild:
    enable: true
    interval_hours: 12
  sug_trie_rebuild:
    enable: true
    interval_sec: 3600
```

## 编译构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 依赖项

| 库 | 用途 | 是否必需 |
|----|------|---------|
| yaml-cpp | 配置解析 | 必需 |
| protobuf | 数据序列化 | 必需 |
| jsoncpp | JSON API 响应 | 必需 |
| sqlite3 | 文档存储 + 统计 | 必需 |
| spdlog | 结构化日志 | 可选 |
| ONNX Runtime | Embedding 模型推理 | 可选（降级到伪向量） |
| LightGBM | 机器学习排序模型 | 可选（降级到规则树） |
| Faiss | 向量近似最近邻搜索 | 可选（降级到暴力搜索） |

## 启动运行

```bash
./minisearchrec --config ./config
```

## API 接口

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/v1/search` | POST | 全文搜索 |
| `/api/v1/sug` | POST | 搜索建议 |
| `/api/v1/hint` | POST | 点后推荐 |
| `/api/v1/nav` | POST | 搜前引导 |
| `/api/v1/doc` | POST/PUT/DELETE | 文档增删改 |
| `/api/v1/event` | POST | 用户行为事件上报 |
| `/api/v1/admin/reload_model` | POST | 排序模型热更新 |

## 开源协议

MIT
