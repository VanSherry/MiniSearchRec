#!/usr/bin/env python3
"""
MiniSearchRec - 排序模型训练脚本
用法: python3 train_rank_model.py --input ./data/train.txt --output ./models/rank_model.txt

输入格式: LightGBM 排序格式 (qid)
  label qid:<hash> 1:f1 2:f2 ... # uid doc_id

特征:
  1: query_len   - 查询长度 (tanh 归一化)
  2: bm25_score  - BM25 相关性分
  3: quality_score - 文档质量分
  4: freshness_score - 时效性分
  5: log_click   - 点击量 (tanh 归一化)
  6: log_like    - 点赞量 (tanh 归一化)
"""

import argparse
import os
import sys
import json

def main():
    parser = argparse.ArgumentParser(description='Train ranking model')
    parser.add_argument('--input', required=True, help='Input training data path')
    parser.add_argument('--output', required=True, help='Output model path')
    parser.add_argument('--incremental', action='store_true', help='Incremental training (not yet supported)')
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: training data not found: {args.input}", file=sys.stderr)
        return 1

    # 检查 lightgbm 是否可用
    try:
        import lightgbm as lgb
    except ImportError:
        print("lightgbm not installed.", file=sys.stderr)
        print("Install it with: pip install lightgbm", file=sys.stderr)
        print("", file=sys.stderr)
        print("Then run:", file=sys.stderr)
        print(f"  python3 {sys.argv[0]} --input {args.input} --output {args.output}", file=sys.stderr)
        return 1

    print(f"[Train] Loading data from {args.input}")
    print(f"[Train] Output model to {args.output}")

    # 加载数据
    group = []
    X, y, qids = [], [], []
    current_qid = None

    with open(args.input, 'r') as f:
        for line in f:
            if line.startswith('#'):
                continue
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            label = int(parts[0])
            qid_str = parts[1]
            qid = int(qid_str.split(':')[1])
            features = []
            for p in parts[2:]:
                if p.startswith('#'):
                    break
                if ':' in p:
                    features.append(float(p.split(':')[1]))

            if current_qid is not None and qid != current_qid:
                group.append(len(X) - sum(group))
            current_qid = qid
            X.append(features)
            y.append(label)
            qids.append(qid)

    if current_qid is not None:
        group.append(len(X) - sum(group))

    total = len(y)
    if total == 0:
        print("Error: no training samples found", file=sys.stderr)
        return 1

    # group: 每个 query 对应的样本数
    group_counts = {}
    for qid_val in qids:
        group_counts[qid_val] = group_counts.get(qid_val, 0) + 1
    group = list(group_counts.values())

    num_features = len(X[0]) if X else 0
    print(f"[Train] Loaded {total} samples, {len(group)} queries, {num_features} features")
    print(f"[Train] Query distribution: {group}")

    import numpy as np
    X_arr, y_arr = np.array(X, dtype=np.float32), np.array(y, dtype=np.float32)

    # 数据量小则不划分验证集
    use_val = total >= 10 and len(group) >= 3
    if use_val:
        split = int(total * 0.8)
        X_train, y_train = X_arr[:split], y_arr[:split]
        X_val, y_val = X_arr[split:], y_arr[split:]
        # 按 query 边界重新计算 groups
        seen = set()
        train_group = []
        val_group = []
        for i, q in enumerate(qids):
            if q not in seen:
                seen.add(q)
                cnt = qids.count(q)
                if i < split:
                    train_group.append(cnt)
                else:
                    val_group.append(cnt)
    else:
        X_train, y_train = X_arr, y_arr
        X_val, y_val = None, None
        train_group = group

    train_data = lgb.Dataset(X_train, label=y_train, group=train_group)
    val_data = None
    valid_sets = None
    callbacks = []
    if use_val and len(X_val) > 0 and len(val_group) > 0:
        val_data = lgb.Dataset(X_val, label=y_val, group=val_group, reference=train_data)
        valid_sets = [val_data]
        callbacks = [lgb.early_stopping(20)]

    # LambdaRank 参数
    params = {
        'objective': 'lambdarank',
        'metric': 'ndcg',
        'ndcg_eval_at': [1, 3, 5],
        'boosting_type': 'gbdt',
        'num_leaves': 31,
        'learning_rate': 0.05,
        'min_data_in_leaf': 1,
        'num_threads': 4,
        'verbose': 1,
    }

    print(f"[Train] Starting LambdaRank training...")
    model = lgb.train(
        params,
        train_data,
        num_boost_round=200,
        valid_sets=valid_sets,
        callbacks=callbacks,
    )

    # 保存模型
    os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
    model.save_model(args.output)
    print(f"[Train] Model saved to {args.output}")

    # 输出特征重要性
    importance = model.feature_importance(importance_type='gain')
    feat_names = ['query_len', 'bm25_score', 'quality_score', 'freshness_score',
                  'log_click', 'log_like']
    print(f"\n[Train] Feature importance (gain):")
    for name, imp in sorted(zip(feat_names, importance), key=lambda x: -x[1]):
        if imp > 0:
            print(f"  {name}: {imp:.1f}")

    print(f"\n[Train] Done! Best NDCG: {model.best_score}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
