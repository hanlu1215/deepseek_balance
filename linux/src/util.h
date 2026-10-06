#pragma once

#include <string>

// 杂项工具。
//
// Windows 版这里最重的是 UTF-8 <-> UTF-16 互转（Win32 API 吃宽字符，接口返回 UTF-8）。
// GTK / Pango / cairo 全程都是 UTF-8，所以这一层直接没有了，只剩字符串打理。

std::string trim(const std::string& text);

// 按「字符」（码点）截断。中文一个字 3 字节，按字节切会切出非法 UTF-8，
// Pango 拿到会报警告甚至显示乱码。
std::string truncateChars(const std::string& text, size_t count);

// 按码点数出字符串长度（不是字节数）。
size_t charCount(const std::string& text);

// 可执行文件所在目录（带结尾斜杠）。用来找图标这类放在程序旁边的资源。
// 取不到（比如 /proc 没挂）就返回空串，调用方自行回退到当前目录。
std::string executableDir();
