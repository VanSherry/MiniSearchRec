// ============================================================
// MiniSearchRec - 文档行为共现存储
// 用于 Hint（点后推）共现召回
// 参考：业界行为共现存储
// ============================================================

#ifndef MINISEARCHREC_DOC_COOCCUR_STORE_H
#define MINISEARCHREC_DOC_COOCCUR_STORE_H

#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace minisearchrec {

struct CooccurItem {
    std::string dst_doc_id;
    int         co_count  = 0;
    int64_t     last_time = 0;
};

class DocCooccurStore {
public:
    static DocCooccurStore& Instance() {
        static DocCooccurStore inst;
        return inst;
    }

    bool Initialize(const std::string& db_path);
    void Close();

    // 记录一次共现（src点击后又点击dst）
    bool RecordCooccurrence(const std::string& src_doc_id,
                            const std::string& dst_doc_id);

    // 获取与 src_doc_id 共现最多的文档列表
    std::vector<CooccurItem> GetTopCooccur(const std::string& src_doc_id, int limit = 20);

private:
    DocCooccurStore() = default;
    ~DocCooccurStore() { Close(); }

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    bool initialized_ = false;
};

} // namespace minisearchrec

#endif // MINISEARCHREC_DOC_COOCCUR_STORE_H
