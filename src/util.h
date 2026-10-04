#pragma once

#include <string>

// UTF-8 <-> UTF-16。接口返回的是 UTF-8 字节，界面和 Win32 API 用的是宽字符。
std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& text);

std::wstring trimWide(const std::wstring& text);
std::string trimNarrow(const std::string& text);

// 按「字符」截断（中文算一个），不会把代理对劈成两半。
std::wstring truncateChars(const std::wstring& text, size_t count);

// exe 所在的目录（带结尾反斜杠）。打包/移动之后音效缓存就落在 exe 旁边。
std::wstring executableDir();

// GetLastError() 之类的系统错误码 -> 人能看懂的一句话。
std::wstring win32ErrorMessage(unsigned long code);
