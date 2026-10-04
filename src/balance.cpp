#include "balance.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdlib>
#include <map>

#include "config.h"
#include "util.h"

namespace {

constexpr const char* kDemoPayload =
    "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"18.23\","
    "\"granted_balance\":\"8.23\",\"topped_up_balance\":\"10.00\"}]}";

// 一个能自动收尾的 WinHTTP 句柄。
class Handle {
public:
    Handle() = default;
    explicit Handle(HINTERNET handle) : handle_(handle) {}
    ~Handle() { reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    void reset(HINTERNET handle = nullptr) {
        if (handle_ != nullptr) WinHttpCloseHandle(handle_);
        handle_ = handle;
    }

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

// 把错误压成一行短句子，塞得进卡片。
std::wstring httpFailure(unsigned int status, const std::string& body) {
    static const std::map<unsigned int, const wchar_t*> kHints = {
        {401, L"Key 无效"},
        {402, L"余额不足"},
        {403, L"无权限"},
        {429, L"请求过频"},
    };

    std::wstring hint;
    const auto found = kHints.find(status);
    if (found != kHints.end()) {
        hint = found->second;
    } else {
        hint = L"HTTP " + std::to_wstring(status);
    }

    // 尽量把服务端给的 message 抠出来，抠不到就用原始正文的前 160 个字节。
    std::wstring message;
    json::Value document;
    if (json::parse(body, document)) {
        const json::Value* error = document.find("error");
        const json::Value* text = error != nullptr ? error->find("message") : nullptr;
        if (text != nullptr) message = toWide(text->asString(""));
    }
    if (message.empty()) {
        message = toWide(trimNarrow(body).substr(0, 160));
    }

    if (message.empty()) return hint;
    return hint + L": " + message;
}

}  // namespace

std::wstring cleanKey(const std::wstring& value) {
    std::wstring text = trimWide(value);
    while (text.size() >= 2 && text.front() == text.back() &&
           (text.front() == L'"' || text.front() == L'\'')) {
        text = trimWide(text.substr(1, text.size() - 2));
    }
    return text;
}

std::wstring rawEnvKey() {
    const DWORD need = GetEnvironmentVariableW(kApiKeyEnv, nullptr, 0);
    if (need == 0) return std::wstring();
    std::wstring buffer(need, L'\0');
    const DWORD written = GetEnvironmentVariableW(kApiKeyEnv, buffer.data(), need);
    if (written == 0 || written >= need) return std::wstring();
    buffer.resize(written);
    return buffer;
}

std::wstring resolveApiKey() {
    if (trimWide(kApiKeyLiteral).size() > 0) return cleanKey(kApiKeyLiteral);
    return cleanKey(rawEnvKey());
}

std::wstring keySource() {
    if (trimWide(kApiKeyLiteral).size() > 0) return L"程序里的 API_KEY";
    if (!resolveApiKey().empty()) return std::wstring(L"系统环境变量 ") + kApiKeyEnv;
    return L"没找到";
}

std::wstring maskKey(const std::wstring& key) {
    if (key.size() <= 8) return std::wstring(key.size(), L'*');
    return key.substr(0, 4) + std::wstring(key.size() - 6, L'*') + key.substr(key.size() - 2);
}

std::wstring shortError(const std::wstring& error) {
    const size_t colon = error.find(L':');
    std::wstring head = trimWide(colon == std::wstring::npos ? error : error.substr(0, colon));
    if (head.empty()) head = error;
    return truncateChars(head, 10);
}

std::string firstBalance(const json::Value& payload) {
    const json::Value* infos = payload.find("balance_infos");
    const json::Value* first = infos != nullptr ? infos->at(0) : nullptr;
    if (first == nullptr) return "--";
    const json::Value* total = first->find("total_balance");
    if (total == nullptr) return "--";
    return total->asString("--");
}

bool parseBalance(const std::string& text, double& out) {
    const std::string trimmed = trimNarrow(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0') return false;   // 尾部还有垃圾字符
    out = value;
    return true;
}

FetchResult fetchBalance(const std::wstring& apiKey) {
    FetchResult result;

    // DEFAULT_PROXY = 走用户当前的系统代理设置（代理/VPN 环境同样能用）。
    // 不用 *_AUTOMATIC_PROXY：它要 Windows 8.1 起才有，而且被 _WIN32_WINNT 挡着。
    Handle session(WinHttpOpen(L"DeepSeekBalanceCube/1.0",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        result.error = L"网络不可达: " + win32ErrorMessage(GetLastError());
        return result;
    }

    const int timeoutMs = kTimeoutSeconds * 1000;
    WinHttpSetTimeouts(session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    Handle connection(WinHttpConnect(session.get(), kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        result.error = L"网络不可达: " + win32ErrorMessage(GetLastError());
        return result;
    }

    Handle request(WinHttpOpenRequest(connection.get(), L"GET", kApiPath, nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE));
    if (!request) {
        result.error = L"网络不可达: " + win32ErrorMessage(GetLastError());
        return result;
    }

    const std::wstring headers = L"Accept: application/json\r\nAuthorization: Bearer " + apiKey + L"\r\n";
    if (!WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(headers.size()),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        result.error = L"网络不可达: " + win32ErrorMessage(GetLastError());
        return result;
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        result.error = L"网络不可达: " + win32ErrorMessage(GetLastError());
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0) break;
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), body.data() + offset, available, &read)) break;
        body.resize(offset + read);
        if (read == 0) break;
    }

    if (status < 200 || status >= 300) {
        result.error = httpFailure(status, body);
        return result;
    }

    json::Value document;
    if (!json::parse(body, document)) {
        result.error = L"返回不是合法 JSON";
        return result;
    }

    result.ok = true;
    result.payload = std::move(body);
    return result;
}

const char* demoPayload() {
    return kDemoPayload;
}
