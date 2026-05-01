// ============================================================
// MiniSearchRec - 搜索接口处理器
// 对应 API：POST /api/v1/search
// ============================================================

#ifndef MINISEARCHREC_SEARCH_HANDLER_H
#define MINISEARCHREC_SEARCH_HANDLER_H

#include <string>
#include "httplib.h"
#include "search.pb.h"

namespace minisearchrec {

class SearchHandler {
public:
    SearchHandler() = default;

    void Handle(const httplib::Request& req, httplib::Response& res);

private:
    bool ParseRequest(const std::string& body, SearchRequest& request);
    int  DoSearch(const SearchRequest& request, SearchResponse& response);
    std::string SerializeResponse(const SearchResponse& response);
};

// ── 模型热更新接口 ──
// 触发 SearchPipeline 单例上所有 LGBMScorerProcessor 的双 Buffer 热更新。
// 返回成功切换的 scorer 数量，0 表示失败或无 LGBM scorer。
// 线程安全：可在服务运行期间随时调用。
int ReloadRankModel(const std::string& new_model_path);

} // namespace minisearchrec

#endif // MINISEARCHREC_SEARCH_HANDLER_H
