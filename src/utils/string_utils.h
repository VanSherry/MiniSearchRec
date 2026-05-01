// ============================================================
// MiniSearchRec - 字符串工具
// 提供分词、大小写转换、trim 等常用字符串操作
// ============================================================

#ifndef MINISEARCHREC_STRING_UTILS_H
#define MINISEARCHREC_STRING_UTILS_H

#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <sstream>

namespace minisearchrec {
namespace utils {

// 大小写转换
std::string ToLower(const std::string& str);
std::string ToUpper(const std::string& str);

// 去除首尾空格
std::string Trim(const std::string& str);
std::string LTrim(const std::string& str);
std::string RTrim(const std::string& str);

// 分词（按空格/标点分割）
std::vector<std::string> Split(const std::string& str, char delimiter = ' ');
std::vector<std::string> Split(const std::string& str, const std::string& delimiters);

// 分词（中文+英文混合，简单按字符切分）
std::vector<std::string> Tokenize(const std::string& text);

// 判断是否包含中文字符
bool ContainsChinese(const std::string& str);

// URL 解码
std::string UrlDecode(const std::string& str);

// 格式化字符串（简单版本）
std::string Format(const char* fmt, ...);

// 字符串替换
std::string ReplaceAll(std::string str, const std::string& from, const std::string& to);

// 判断字符串是否为空或全是空格
bool IsBlank(const std::string& str);

// UTF-8 安全截断（不会在多字节字符中间截断）
std::string Utf8Truncate(const std::string& str, size_t max_bytes);

} // namespace utils
} // namespace minisearchrec

#endif // MINISEARCHREC_STRING_UTILS_H
