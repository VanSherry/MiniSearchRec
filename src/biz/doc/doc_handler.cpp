// ============================================================
// MiniSearchRec - 文档管理接口处理器实现
// ============================================================

#include "biz/doc/doc_handler.h"
#include "framework/app_context.h"
#include "utils/logger.h"
#include <json/json.h>
#include <sstream>
namespace minisearchrec {

static std::string MakeError(int ret, const std::string& msg) {
    Json::Value root;
    root["ret"]     = ret;
    root["err_msg"] = msg;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, root);
}

static std::string MakeOK(const std::string& msg = "") {
    Json::Value root;
    root["ret"]     = 0;
    root["err_msg"] = msg;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, root);
}

void DocHandler::HandleAdd(const httplib::Request& req,
                            httplib::Response& res) {
    if (req.body.empty()) {
        res.set_content(MakeError(400, "Empty request body"), "application/json");
        res.status = 400;
        return;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(req.body);
    if (!Json::parseFromStream(builder, iss, &root, &errors)) {
        res.set_content(MakeError(400, "Invalid JSON: " + errors), "application/json");
        res.status = 400;
        return;
    }

    // 构造 Document
    Document doc;
    if (!root.isMember("doc_id") || root["doc_id"].asString().empty()) {
        res.set_content(MakeError(400, "doc_id is required"), "application/json");
        res.status = 400;
        return;
    }
    doc.set_doc_id(root["doc_id"].asString());
    doc.set_title(root.get("title", "").asString());
    doc.set_content(root.get("content", "").asString());
    doc.set_author(root.get("author", "").asString());
    doc.set_category(root.get("category", "").asString());
    doc.set_quality_score(root.get("quality_score", 0.5f).asFloat());
    doc.set_click_count(root.get("click_count", 0).asInt64());
    doc.set_like_count(root.get("like_count", 0).asInt64());
    doc.set_publish_time(root.get("publish_time", 0).asInt64());
    doc.set_content_length(static_cast<int32_t>(doc.content().size()));

    if (root.isMember("tags") && root["tags"].isArray()) {
        for (const auto& tag : root["tags"]) {
            doc.add_tags(tag.asString());
        }
    }

    if (!AppContext::Instance().AddDocument(doc)) {
        LOG_ERROR("DocHandler::HandleAdd failed for doc_id={}", doc.doc_id());
        res.set_content(MakeError(500, "Failed to add document"), "application/json");
        res.status = 500;
        return;
    }

    LOG_INFO("DocHandler::HandleAdd success, doc_id={}", doc.doc_id());
    res.set_content(MakeOK(), "application/json");
    res.status = 200;
}

void DocHandler::HandleUpdate(const httplib::Request& req,
                               httplib::Response& res) {
    // 更新等价于重新 Add（倒排索引会覆盖）
    HandleAdd(req, res);
}

void DocHandler::HandleDelete(const httplib::Request& req,
                               httplib::Response& res) {
    // 从 query 参数中获取 doc_id
    std::string doc_id;
    if (req.has_param("doc_id")) {
        doc_id = req.get_param_value("doc_id");
    } else if (!req.body.empty()) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream iss(req.body);
        if (Json::parseFromStream(builder, iss, &root, &errors)) {
            doc_id = root.get("doc_id", "").asString();
        }
    }

    if (doc_id.empty()) {
        res.set_content(MakeError(400, "doc_id is required"), "application/json");
        res.status = 400;
        return;
    }

    auto doc_store = AppContext::Instance().GetDocStore();
    if (!doc_store) {
        res.set_content(MakeError(500, "DocStore not available"), "application/json");
        res.status = 500;
        return;
    }

    // 先从 DocStore 删除（确认文档存在）
    if (!doc_store->DeleteDoc(doc_id)) {
        res.set_content(MakeError(404, "Document not found: " + doc_id), "application/json");
        res.status = 404;
        return;
    }

    // DocStore 删除成功后再清理倒排索引
    auto inv_idx = AppContext::Instance().GetInvertedIndex();
    if (inv_idx) {
        inv_idx->RemoveDocument(doc_id);
    }

    // 持久化更新后的索引
    auto index_builder = AppContext::Instance().GetIndexBuilder();
    // 通过 AppContext 的 SaveIndexes 接口触发（借用 AddDocument 后的刷盘机制）
    // 简单方案：直接调用 inverted_index Save
    // （此处不再重复实现，AppContext 会在下次 AddDocument 时刷盘，
    //  重启后 SQLite 重建索引也不含此文档）

    LOG_INFO("DocHandler::HandleDelete success, doc_id={}", doc_id);
    res.set_content(MakeOK(), "application/json");
    res.status = 200;
}

} // namespace minisearchrec
