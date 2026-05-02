// ============================================================
// MiniSearchRec - 字符串工具实现
// ============================================================

#include "utils/string_utils.h"
#include <cstdio>
#include <cstdarg>
#include <iomanip>
#include <sstream>

namespace minisearchrec {
namespace utils {

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string ToUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string Trim(const std::string& str) {
    return LTrim(RTrim(str));
}

std::string LTrim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    return (start == std::string::npos) ? "" : str.substr(start);
}

std::string RTrim(const std::string& str) {
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return (end == std::string::npos) ? "" : str.substr(0, end + 1);
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::vector<std::string> Split(const std::string& str, const std::string& delimiters) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t pos = str.find_first_of(delimiters, start);
    
    while (pos != std::string::npos) {
        if (pos > start) {
            tokens.push_back(str.substr(start, pos - start));
        }
        start = pos + 1;
        pos = str.find_first_of(delimiters, start);
    }
    
    if (start < str.length()) {
        tokens.push_back(str.substr(start));
    }
    
    return tokens;
}

std::vector<std::string> Tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    
    // 简单分词：英文按空格和标点分割，中文按字分割
    std::string current_token;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = text[i];
        
        // 中文字符（UTF-8 三字节）
        if ((c & 0xE0) == 0xC0) {
            // 两字节 UTF-8
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            if (i + 1 < text.length()) {
                tokens.push_back(text.substr(i, 2));
                i += 2;
            } else {
                i++;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 三字节 UTF-8（中文）
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            if (i + 2 < text.length()) {
                tokens.push_back(text.substr(i, 3));
                i += 3;
            } else {
                i++;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 四字节 UTF-8
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            if (i + 3 < text.length()) {
                tokens.push_back(text.substr(i, 4));
                i += 4;
            } else {
                i++;
            }
        } else if (std::isalnum(c) || c == '\'') {
            // 英文单词字符
            current_token += c;
            i++;
        } else {
            // 分隔符
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            i++;
        }
    }
    
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }
    
    return tokens;
}

bool ContainsChinese(const std::string& str) {
    for (size_t i = 0; i < str.length(); ) {
        unsigned char c = str[i];
        if (c < 0x80) {
            ++i;
        } else if (c < 0xE0) {
            i += 2;
        } else if (c < 0xF0) {
            // 三字节 UTF-8：检查是否在常用中文 Unicode 范围内
            if (i + 2 < str.length()) {
                uint32_t code = (static_cast<uint32_t>(str[i]) & 0x0F) << 12 |
                               (static_cast<uint32_t>(str[i+1]) & 0x3F) << 6  |
                               (static_cast<uint32_t>(str[i+2]) & 0x3F);
                if (code >= 0x4E00 && code <= 0x9FFF) {
                    return true;
                }
            }
            i += 3;
        } else {
            i += 4;
        }
    }
    return false;
}

std::string UrlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream hex_stream(str.substr(i + 1, 2));
            if (hex_stream >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string Format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    // 先获取所需缓冲区大小
    va_list args_copy;
    va_copy(args_copy, args);
    int size = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (size < 0) {
        va_end(args);
        return "";
    }
    
    std::string result(size + 1, '\0');
    std::vsnprintf(&result[0], size + 1, fmt, args);
    va_end(args);
    
    result.resize(size);
    return result;
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

bool IsBlank(const std::string& str) {
    return std::all_of(str.begin(), str.end(), [](unsigned char c) {
        return std::isspace(c);
    });
}

std::string Utf8Truncate(const std::string& str, size_t max_bytes) {
    if (str.size() <= max_bytes) return str;
    // 从 max_bytes 向前找到完整 UTF-8 字符边界
    size_t i = max_bytes;
    // 只要当前字节是 UTF-8 延续字节（10xxxxxx），就向前移
    while (i > 0 && (static_cast<unsigned char>(str[i]) & 0xC0) == 0x80) {
        --i;
    }
    // i 现在指向一个 UTF-8 起始字节或 ASCII；退回一个完整字符
    // 如果 i < max_bytes 说明我们退过了延续字节，i 已在起始字节，直接截到 i
    return str.substr(0, i);
}

size_t Utf8Len(const std::string& str) {
    size_t len = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = str[i];
        if (c < 0x80)      { i += 1; }
        else if (c < 0xE0) { i += 2; }
        else if (c < 0xF0) { i += 3; }
        else               { i += 4; }
        ++len;
    }
    return len;
}

} // namespace utils
} // namespace minisearchrec
