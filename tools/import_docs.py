#!/usr/bin/env python3
"""
MiniSearchRec - 文档导入脚本
将各种格式的文档导入为项目可用的 JSON 格式

用法：
    python import_docs.py --input data.csv --output data/docs.json
    python import_docs.py --input data/ --output data/docs.json --recursive
"""

import argparse
import csv
import json
import os
import re
from datetime import datetime
from pathlib import Path

def clean_text(text):
    """清理文本：去除多余空白"""
    text = re.sub(r'\s+', ' ', text.strip())
    return text

def csv_to_docs(csv_path):
    """从 CSV 文件读取文档"""
    docs = []
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            doc = {
                "doc_id": row.get("doc_id", f"doc_{len(docs)+1}"),
                "title": clean_text(row.get("title", "")),
                "content": clean_text(row.get("content", "")),
                "author": row.get("author", "unknown"),
                "publish_time": int(datetime.strptime(
                    row.get("publish_time", "2024-01-01"),
                    "%Y-%m-%d"
                ).timestamp()),
                "category": row.get("category", "general"),
                "tags": [t.strip() for t in row.get("tags", "").split(",") if t.strip()],
                "quality_score": float(row.get("quality_score", 0.5)),
                "click_count": int(row.get("click_count", 0)),
                "like_count": int(row.get("like_count", 0)),
            }
            docs.append(doc)
    return docs

def dir_to_docs(dir_path):
    """从目录读取文档（每个文件一篇）"""
    docs = []
    for file_path in Path(dir_path).rglob("*.txt"):
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()

        doc_id = file_path.stem
        title = file_path.stem.replace('_', ' ').title()

        doc = {
            "doc_id": doc_id,
            "title": title,
            "content": clean_text(content[:5000]),  # 限制长度
            "author": "unknown",
            "publish_time": int(file_path.stat().st_mtime),
            "category": "general",
            "tags": [],
            "quality_score": 0.5,
            "click_count": 0,
            "like_count": 0,
        }
        docs.append(doc)
    return docs

def main():
    parser = argparse.ArgumentParser(description="Import documents for MiniSearchRec")
    parser.add_argument("--input", "-i", required=True, help="Input file or directory")
    parser.add_argument("--output", "-o", default="data/docs.json", help="Output JSON file")
    parser.add_argument("--recursive", "-r", action="store_true", help="Recursively scan directory")
    args = parser.parse_args()

    docs = []
    input_path = Path(args.input)

    if input_path.is_file():
        if input_path.suffix == ".csv":
            docs = csv_to_docs(args.input)
        elif input_path.suffix == ".json":
            with open(args.input, 'r') as f:
                docs = json.load(f)
        else:
            print(f"Unsupported file type: {input_path.suffix}")
            return 1
    elif input_path.is_dir():
        docs = dir_to_docs(args.input)
    else:
        print(f"Input not found: {args.input}")
        return 1

    # 确保输出目录存在
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(docs, f, ensure_ascii=False, indent=2)

    print(f"Imported {len(docs)} documents to {args.output}")
    return 0

if __name__ == "__main__":
    exit(main())
