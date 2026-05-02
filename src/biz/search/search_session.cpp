// ============================================================
// MiniSearchRec - SearchSession 业务实现
// GenerateTraceId 已上提到 framework::Session（通用能力）
// 本文件用于 SearchSession 的业务专有方法实现
// ============================================================

#include "biz/search/search_session.h"
#include "framework/class_register.h"

namespace minisearchrec {

// SearchSession 暂无额外业务方法需要实现

} // namespace minisearchrec

// 注册到框架 Session 反射表（配置驱动创建）
using namespace minisearchrec;
REGISTER_MSR_SESSION(SearchSession);
