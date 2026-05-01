# MiniSearchRec 学习路径

> 从零开始，四周掌握搜索推荐系统核心原理

---

## 学习前准备

### 必备知识
- C++17 基础（智能指针、lambda、STL 容器）
- 基本数据结构（哈希表、链表、堆）
- 基本算法（排序、二分查找）

### 开发环境
```bash
# 安装依赖（Ubuntu/Debian）
sudo apt-get install -y build-essential cmake git \
    libprotobuf-dev protobuf-compiler \
    libyaml-cpp-dev libsqlite3-dev

# 克隆项目
git clone https://github.com/VanSherry/MiniSearchRec.git
cd MiniSearchRec
```

---

## 第一周：基础骨架

### 学习目标
理解搜索系统的基本漏斗：**召回 → 排序 → 返回结果**

### 学习内容

#### Day 1-2：理解倒排索引
- 阅读 `src/index/inverted_index.h/.cpp`
- 手画倒排索引结构图：
```
Term: "深度" -> [doc_001, doc_005, doc_023]
Term: "学习" -> [doc_001, doc_003, doc_005, doc_012]
```
- 运行单元测试：`tests/test_inverted_index.cpp`

#### Day 3-4：理解 BM25 算法
- 阅读 `src/rank/bm25_scorer.h/.cpp`
- 推导 BM25 公式：
```
BM25 = Σ IDF(qi) × (f × (k1+1)) / (f + k1 × (1-b+b×|d|/avgdl))
```
- 思考：为什么要有 `b` 参数（文档长度归一化）？

#### Day 5-7：跑通第一个搜索请求
- 阅读 `src/service/search_handler.cpp`
- 编译运行项目：
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j4
./minisearchrec --config ../config
```
- 测试搜索接口：
```bash
curl -X POST <http://localhost:8080/api/v1/search> \
  -H "Content-Type: application/json" \
  -d '{"query": "深度学习", "page": 1, "page_size": 5}'
```

---

## 第二周：个性化能力

### 学习目标
理解用户画像和协同过滤召回

### 学习内容

#### Day 1-2：理解用户画像
- 阅读 `proto/user.proto`
- 阅读 `src/user/user_profile.h/.cpp`（待实现）
- 思考：如何从重放用户行为日志中构建兴趣向量？

#### Day 3-4：实现用户历史召回
- 阅读 `src/recall/user_history_recall.h/.cpp`
- 完善 `UserHistoryRecallProcessor::Process()`：
  - 从用户历史点击中获取 doc_id 列表
  - 查找与这些文档相似的其他文档（简单版：直接推荐历史文档）

#### Day 5-7：配置化召回策略
- 修改 `config/recall.yaml`，调整 `max_recall` 参数
- 观察不同召回策略对结果的影响
- 尝试新增一个召回处理器（参考 `docs/adding_new_processor.md`）

---

## 第三周：向量化能力

### 学习目标
理解语义检索和 Faiss 向量索引

### 学习内容

#### Day 1-2：理解向量索引
- 阅读 `src/index/vector_index.h/.cpp`
- 理解 Faiss HNSW 算法（Hierarchical Navigable Small World）
- 思考：为什么向量检索要用 ANN 而不是精确检索？

#### Day 3-4：集成文档向量
- 为 `proto/doc.proto` 中的 `Document` 添加 `embedding` 字段
- 修改 `IndexBuilder`，在构建索引时同时构建向量索引
- （可选）使用 ONNX Runtime 运行 BERT 模型生成向量

#### Day 5-7：多路召回融合
- 阅读 `src/recall/recall_fusion.cpp`
- 理解 RRF（Reciprocal Rank Fusion）算法：
```
RRFscore(d) = Σ 1 / (k + rank_i(d))
```
- 调整 `config/recall.yaml`，开启 `VectorRecallProcessor`

---

## 第四周：排序模型

### 学习目标
理解机器学习排序（Learning to Rank）和多样性重排

### 学习内容

#### Day 1-2：特征工程
- 阅读 `src/feature/` 目录下的特征提取器
- 理解三类特征：
  - **Query 特征**：query 长度、是否长尾查询
  - **User 特征**：用户活跃度、偏好分类
  - **Doc 特征**：BM25 分、质量分、点击数
  - **交叉特征**：Query-Doc 相似度、User-Doc 相似度

#### Day 3-4：LightGBM 排序模型
- 从线上日志生成训练数据（`tools/generate_training_data.py`）
- 训练 LightGBM 模型：
```bash
python tools/train_model.py \
  --train-data data/training_data.csv \
  --model-out models/rank_model.txt
```
- 修改 `src/rank/lgbm_ranker.cpp`，加载模型并推理

#### Day 5-7：多样性重排
- 阅读 `src/rank/mmr_reranker.cpp`
- 理解 MMR（Maximal Marginal Relevance）：
```
MMR(d) = λ × relevance(d, q) - (1-λ) × max sim(d, d_j)
```
- 调整 `config/rank.yaml` 中的 `lambda` 参数，观察结果多样性变化

---

## 进阶学习（V1/V2 阶段）

### 向量检索深入理解
- 阅读 Faiss 文档：https://github.com/facebookresearch/faiss
- 理解 HNSW 的构建和查询过程
- 尝试调整 `ef_construction` 和 `ef_search` 参数

### 协同过滤深入理解
- 阅读 X(Twitter) 的 UTEG 论文
- 实现基于物品的协同过滤（Item-based CF）
- 预计算用户相似度矩阵

### 实时特征
- 理解 X 的 FeatureUnion 设计
- 实现用户行为的实时特征更新
- 使用 Redis 存储实时特征

### A/B 实验
- 阅读 `src/ab/ab_test.h/.cpp`
- 实现一致性 Hash 分桶
- 设计实验：对比 BM25 和 LightGBM 的点击率

---

## 参考资源

### 论文
- **BM25**: Robertson & Zaragoza (2009) - "The Probabilistic Relevance Framework"
- **LightGBM**: Ke et al. (2017) - "LightGBM: A Highly Efficient Gradient Boosting Decision Tree"
- **MMR**: Carbonell & Goldstein (1998) - "The Use of MMR for Diverse Document Retrieval"

### 工业系统参考
- **X (Twitter) 推荐算法**: https://github.com/twitter/the-algorithm
- **微信搜推**: 参考 mmsearchqxcommon 设计文档
- **Faiss**: https://github.com/facebookresearch/faiss

### 在线课程
- Stanford CS276: Information Retrieval
- Coursera: Machine Learning for Search and Recommendation

---

## 常见问题

### Q: 编译时找不到 json/json.h？
A: 安装 jsoncpp：`sudo apt-get install libjsoncpp-dev`

### Q: 搜索结果为空？
A: 确保已构建索引：`./build/tools/build_index --input data/sample_docs.json`

### Q: 如何添加新的召回策略？
A: 参考 `docs/adding_new_processor.md`（待编写）

### Q: 项目与工业系统的差距在哪？
A: 本项目是学习版本，主要差距在：
- 规模（本项目支持 10 万文档，微信支持亿级）
- 实时性（本项目批量更新，工业系统近实时）
- 模型复杂度（本项目用 LightGBM，工业用深度神经网络）
