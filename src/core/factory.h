// ============================================================
// MiniSearchRec - 处理器工厂头文件
// 声明 RegisterBuiltinProcessors
// ============================================================

#ifndef MINISEARCHREC_FACTORY_H
#define MINISEARCHREC_FACTORY_H

namespace minisearchrec {

// 批量注册所有内置处理器
// 在程序启动时调用一次
void RegisterBuiltinProcessors();

} // namespace minisearchrec

#endif // MINISEARCHREC_FACTORY_H
