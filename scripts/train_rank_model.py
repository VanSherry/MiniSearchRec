#!/usr/bin/env python3
# ============================================================
# MiniSearchRec - LightGBM LambdaRank 训练脚本
#
# 用法：
#   python3 scripts/train_rank_model.py [options]
#
# 选项：
#   --input  <path>   LTR 训练数据文件（dump_train_data 输出）
#                     default: ./data/train.txt
#   --output <path>   模型输出路径  default: ./models/rank_model.txt
#   --eval   <path>   验证集（可选，同格式）
#   --num-trees <n>   树的数量      default: 100
#   --leaves    <n>   每棵树叶节点数 default: 31
#   --lr        <f>   学习率        default: 0.05
#   --incremental     增量训练（在已有模型基础上继续训练）
#   --help
# ============================================================

import argparse
import os
import sys
import time
import shutil
from pathlib import Path

# ============================================================
# 依赖检查
# ============================================================
try:
    import lightgbm as lgb
    import numpy as np
except ImportError:
    print("[train] lightgbm / numpy not installed.")
    print("  Install: pip install lightgbm numpy")
    sys.exit(1)


# ============================================================
# LTR 文件解析
# 格式：label qid:<qid> 1:f1 2:f2 ... # comment
# ============================================================
def parse_ltr_file(path: str):
    """
    Returns:
        X: np.ndarray (N, num_features)
        y: np.ndarray (N,) int labels
        qids: list[int]  query group ids
    """
    X_rows, y_rows, qid_rows = [], [], []

    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            # 去掉行尾注释
            if " # " in line:
                line = line[:line.index(" # ")]

            parts = line.split()
            label = int(parts[0])

            # qid:xxxxx
            qid = 0
            for p in parts[1:]:
                if p.startswith("qid:"):
                    qid = int(p[4:])
                    break

            # feat_idx:feat_val
            feats = {}
            for p in parts[1:]:
                if ":" in p and not p.startswith("qid:"):
                    idx, val = p.split(":", 1)
                    feats[int(idx)] = float(val)

            if not feats:
                continue

            max_idx = max(feats.keys())
            row = [feats.get(i, 0.0) for i in range(1, max_idx + 1)]

            X_rows.append(row)
            y_rows.append(label)
            qid_rows.append(qid)

    if not X_rows:
        return None, None, None

    # 对齐特征维度
    max_len = max(len(r) for r in X_rows)
    X_rows = [r + [0.0] * (max_len - len(r)) for r in X_rows]

    return (np.array(X_rows, dtype=np.float32),
            np.array(y_rows, dtype=np.int32),
            qid_rows)


def build_group(qid_rows):
    """将 qid 列表转为 LightGBM group 数组（每个 query 的样本数）"""
    if not qid_rows:
        return []
    groups = []
    cur_qid = qid_rows[0]
    cnt = 1
    for q in qid_rows[1:]:
        if q == cur_qid:
            cnt += 1
        else:
            groups.append(cnt)
            cur_qid = q
            cnt = 1
    groups.append(cnt)
    return groups


# ============================================================
# 模型版本管理：保留最近 5 个版本
# ============================================================
def save_model_with_version(model, output_path: str):
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    # 备份历史版本
    versions_dir = os.path.join(os.path.dirname(output_path) or ".", "versions")
    os.makedirs(versions_dir, exist_ok=True)

    if os.path.exists(output_path):
        ts = int(time.time())
        base = os.path.basename(output_path)
        backup = os.path.join(versions_dir, f"{base}.{ts}")
        shutil.copy2(output_path, backup)

        # 保留最近 5 个版本
        all_versions = sorted(Path(versions_dir).glob(f"{base}.*"))
        for old in all_versions[:-5]:
            old.unlink()

    model.save_model(output_path)
    print(f"[train] Model saved to {output_path}")

    # 同时保存特征名称文件（供调试）
    feat_names_path = output_path.replace(".txt", ".feat_names.txt")
    feat_names = [
        "query_len",
        "bm25_score",
        "quality_score",
        "freshness_score",
        "log_click",
        "log_like",
    ]
    with open(feat_names_path, "w") as f:
        for name in feat_names:
            f.write(name + "\n")
    print(f"[train] Feature names saved to {feat_names_path}")


# ============================================================
# 训练主函数
# ============================================================
def train(args):
    print(f"[train] Loading training data from {args.input}")
    X, y, qids = parse_ltr_file(args.input)
    if X is None:
        print(f"[train] ERROR: No samples loaded from {args.input}")
        sys.exit(1)

    groups = build_group(qids)
    print(f"[train] Loaded {len(y)} samples, {len(groups)} queries")
    print(f"[train] Label distribution: "
          + str({int(v): int(c) for v, c in zip(*np.unique(y, return_counts=True))}))

    train_data = lgb.Dataset(
        X, label=y, group=groups,
        feature_name=["query_len", "bm25", "quality",
                      "freshness", "log_click", "log_like"],
        free_raw_data=False
    )

    # 验证集（可选）
    valid_sets = [train_data]
    valid_names = ["train"]
    if args.eval and os.path.exists(args.eval):
        print(f"[train] Loading eval data from {args.eval}")
        Xv, yv, qv = parse_ltr_file(args.eval)
        if Xv is not None:
            eval_data = lgb.Dataset(Xv, label=yv, group=build_group(qv),
                                    reference=train_data)
            valid_sets.append(eval_data)
            valid_names.append("eval")

    # ---- LightGBM 参数 ----
    params = {
        # 目标：LambdaRank（NDCG 优化，工业标准排序算法）
        "objective":         "lambdarank",
        "metric":            "ndcg",
        "ndcg_eval_at":      [1, 3, 5, 10],
        "lambdarank_truncation_level": 10,

        # 树结构
        "num_leaves":        args.leaves,
        "max_depth":         -1,
        "min_child_samples": 5,

        # 正则化（防止小数据集过拟合）
        "lambda_l1":         0.1,
        "lambda_l2":         0.1,
        "feature_fraction":  0.8,
        "bagging_fraction":  0.8,
        "bagging_freq":      5,

        # 学习率
        "learning_rate":     args.lr,

        # 多线程
        "num_threads":       4,
        "verbosity":         1,
    }

    print(f"\n[train] Training LambdaRank model:")
    print(f"  num_trees={args.num_trees}, num_leaves={args.leaves}, lr={args.lr}")

    # 增量训练：在已有模型基础上继续
    init_model = None
    if args.incremental and os.path.exists(args.output):
        print(f"[train] Incremental training from {args.output}")
        init_model = args.output

    callbacks = [lgb.log_evaluation(period=10)]
    if valid_sets:
        callbacks.append(lgb.early_stopping(stopping_rounds=20, verbose=True))

    model = lgb.train(
        params,
        train_data,
        num_boost_round=args.num_trees,
        valid_sets=valid_sets,
        valid_names=valid_names,
        callbacks=callbacks,
        init_model=init_model,
    )

    # ---- 特征重要性 ----
    print("\n[train] Feature importance (gain):")
    feat_names = ["query_len", "bm25", "quality", "freshness", "log_click", "log_like"]
    importance = model.feature_importance(importance_type="gain")
    total = importance.sum() + 1e-9
    for name, imp in sorted(zip(feat_names, importance),
                             key=lambda x: -x[1]):
        bar = "█" * int(imp / total * 30)
        print(f"  {name:<18} {imp:8.2f}  {bar}")

    # ---- 保存模型 ----
    save_model_with_version(model, args.output)

    print(f"\n[train] Training complete!")
    print(f"[train] Next steps:")
    print(f"  1. Update config/rank.yaml: uncomment LGBMScorerProcessor")
    print(f"  2. Set model_path: {args.output}")
    print(f"  3. Rebuild and restart: cmake --build build && ./build/minisearchrec")
    print(f"  4. Re-run periodically: cron daily → dump_train_data + train_rank_model.py")


# ============================================================
# 入口
# ============================================================
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="MiniSearchRec - LightGBM LambdaRank Trainer"
    )
    parser.add_argument("--input",       default="./data/train.txt")
    parser.add_argument("--output",      default="./models/rank_model.txt")
    parser.add_argument("--eval",        default="")
    parser.add_argument("--num-trees",   type=int,   default=100)
    parser.add_argument("--leaves",      type=int,   default=31)
    parser.add_argument("--lr",          type=float, default=0.05)
    parser.add_argument("--incremental", action="store_true")
    args = parser.parse_args()

    train(args)
