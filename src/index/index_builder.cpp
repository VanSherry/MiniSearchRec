// ============================================================
// MiniSearchRec - 索引构建器实现
// ============================================================

#include "index/index_builder.h"
#include "core/app_context.h"
#include <json/json.h>
#include <fstream>
#include <iostream>

namespace minisearchrec {

bool IndexBuilder::BuildFromJson(const std::string& json_path) {
    std::ifstream ifs(json_path);
    if (!ifs) {
        std::cerr << "[IndexBuilder] Failed to open: " << json_path << "\n";
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, ifs, &root, &errs)) {
        std::cerr << "[IndexBuilder] Failed to parse JSON: " << errs << "\n";
        return false;
    }

    if (!root.isArray()) {
        std::cerr << "[IndexBuilder] Expected JSON array\n";
        return false;
    }

    std::vector<Document> docs;
    for (int i = 0; i < (int)root.size(); ++i) {
        const auto& item = root[i];
        Document doc;

        doc.set_doc_id(item["doc_id"].asString());
        doc.set_title(item["title"].asString());
        doc.set_content(item["content"].asString());
        doc.set_author(item["author"].asString());
        doc.set_publish_time(item["publish_time"].asInt64());
        doc.set_category(item["category"].asString());
        doc.set_quality_score(item["quality_score"].asFloat());
        doc.set_click_count(item["click_count"].asInt64());
        doc.set_like_count(item["like_count"].asInt64());
        doc.set_content_length(static_cast<int32_t>(item["content"].asString().size()));

        const auto& tags = item["tags"];
        if (tags.isArray()) {
            for (int j = 0; j < (int)tags.size(); ++j) {
                doc.add_tags(tags[j].asString());
            }
        }

        docs.push_back(doc);
    }

    return BuildFromDocs(docs);
}

bool IndexBuilder::BuildFromDocs(const std::vector<Document>& docs) {
    if (!inv_idx_ || !doc_store_) {
        std::cerr << "[IndexBuilder] Index or DocStore not set\n";
        return false;
    }

    std::cout << "[IndexBuilder] Building index for " << docs.size()
              << " documents...\n";

    for (const auto& doc : docs) {
        // 添加到倒排索引
        std::vector<std::string> tags;
        for (int i = 0; i < doc.tags_size(); ++i) {
            tags.push_back(doc.tags(i));
        }

        inv_idx_->AddDocument(
            doc.doc_id(),
            doc.title(),
            doc.content(),
            doc.category(),
            tags,
            doc.content_length()
        );

        // 添加到向量索引（通过 EmbeddingProvider 生成向量）
        if (vec_idx_) {
            auto provider = AppContext::Instance().GetEmbeddingProvider();
            if (provider) {
                auto emb = provider->Encode(doc.title() + " " + doc.content());
                vec_idx_->AddVector(doc.doc_id(), emb);
            }
        }

        // 存储到 SQLite
        doc_store_->PutDoc(doc);
    }

    std::cout << "[IndexBuilder] Index build complete. Total docs: "
              << inv_idx_->GetDocCount() << "\n";
    return true;
}

bool IndexBuilder::AddDocument(const Document& doc) {
    if (!inv_idx_ || !doc_store_) return false;

    std::vector<std::string> tags;
    for (int i = 0; i < doc.tags_size(); ++i) {
        tags.push_back(doc.tags(i));
    }

    // BUG-12 修复：先持久化，成功后再更新内存索引（Write-ahead 保证重启一致性）
    if (!doc_store_->PutDoc(doc)) {
        std::cerr << "[IndexBuilder] PutDoc failed for doc_id=" << doc.doc_id() << "\n";
        return false;
    }

    // AddDocument 内部已实现幂等（先 RemoveDocument 再重建），BUG-2/19 同时修复
    inv_idx_->AddDocument(
        doc.doc_id(),
        doc.title(),
        doc.content(),
        doc.category(),
        tags,
        doc.content_length()
    );

    // 添加到向量索引（通过 EmbeddingProvider 生成向量）
    if (vec_idx_) {
        auto provider = AppContext::Instance().GetEmbeddingProvider();
        if (provider) {
            auto emb = provider->Encode(doc.title() + " " + doc.content());
            vec_idx_->AddVector(doc.doc_id(), emb);
        }
    }

    return true;
}

bool IndexBuilder::SaveIndexes(const std::string& index_dir) {
    if (!inv_idx_) return false;

    std::string inv_path = index_dir + "/inverted.idx";
    std::string vec_path = index_dir + "/vector.faiss";

    bool ok = inv_idx_->Save(inv_path);
    if (vec_idx_) {
        ok = ok && vec_idx_->Save(vec_path);
    }

    std::cout << "[IndexBuilder] Indexes saved to " << index_dir << "\n";
    return ok;
}

bool IndexBuilder::LoadIndexes(const std::string& index_dir) {
    if (!inv_idx_) return false;

    std::string inv_path = index_dir + "/inverted.idx";
    std::string vec_path = index_dir + "/vector.faiss";

    bool ok = inv_idx_->Load(inv_path);
    if (vec_idx_) {
        ok = ok && vec_idx_->Load(vec_path);
    }

    std::cout << "[IndexBuilder] Indexes loaded from " << index_dir << "\n";
    return ok;
}

} // namespace minisearchrec
