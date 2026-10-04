#pragma once

#include <string>

#include "json.h"

// 后台线程取回来的结果。
struct FetchResult {
    bool ok = false;
    std::string payload;    // UTF-8 JSON，ok 为 true 时有效
    std::wstring error;     // ok 为 false 时的中文短句，例如「Key 无效: ...」
};

// 去掉首尾空白，以及误加进去的引号。
// `set DEEPSEEK_API_KEY="sk-xxx"` 这种写法会把引号一起存进环境变量，
// 带着引号去请求必然 401，所以这里统一清一遍。
std::wstring cleanKey(const std::wstring& value);

// 程序里的 kApiKeyLiteral 优先，其次环境变量 DEEPSEEK_API_KEY。
std::wstring resolveApiKey();

// 环境变量里原封不动的值（没清理过），排查「是不是带了引号」时用。
std::wstring rawEnvKey();

// Key 是从哪儿读到的，排查时先看这一行。
std::wstring keySource();

// 只露头尾，用来确认「读到的到底是哪一串」。
std::wstring maskKey(const std::wstring& key);

// 把错误压成一行短标签，好塞进卡片里显示。
std::wstring shortError(const std::wstring& error);

// 余额 -> "18.23"；拿不到就是 "--"。
std::string firstBalance(const json::Value& payload);

// "18.23" -> 18.23；是 "--" 之类解析不了就返回 false。
bool parseBalance(const std::string& text, double& out);

// 请求 /user/balance。失败时把中文原因写进 error。
FetchResult fetchBalance(const std::wstring& apiKey);

// --demo 用的假数据。
const char* demoPayload();
