#include "balance.h"

#include <gio/gio.h>

#include <cstdlib>
#include <map>

#include "config.h"
#include "util.h"

namespace {

constexpr const char* kDemoPayload =
    "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"18.23\","
    "\"granted_balance\":\"8.23\",\"topped_up_balance\":\"10.00\"}]}";

// 一个能自动收尾的 GObject 句柄（对应 Windows 版的 Handle）。
template <typename T>
class Ref {
public:
    explicit Ref(T* object = nullptr) : object_(object) {}
    ~Ref() {
        if (object_ != nullptr) g_object_unref(object_);
    }

    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;

    T* get() const { return object_; }
    explicit operator bool() const { return object_ != nullptr; }

private:
    T* object_;
};

// 把错误压成一行短句子，塞得进卡片。
std::string httpFailure(unsigned int status, const std::string& body) {
    static const std::map<unsigned int, const char*> kHints = {
        {401, "Key 无效"},
        {402, "余额不足"},
        {403, "无权限"},
        {429, "请求过频"},
    };

    std::string hint;
    const auto found = kHints.find(status);
    if (found != kHints.end()) {
        hint = found->second;
    } else {
        hint = "HTTP " + std::to_string(status);
    }

    // 尽量把服务端给的 message 抠出来，抠不到就用原始正文的前 160 个字节。
    std::string message;
    json::Value document;
    if (json::parse(body, document)) {
        const json::Value* error = document.find("error");
        const json::Value* text = error != nullptr ? error->find("message") : nullptr;
        if (text != nullptr) message = text->asString("");
    }
    if (message.empty()) {
        message = truncateChars(trim(body), 160);
    }

    if (message.empty()) return hint;
    return hint + ": " + message;
}

// GIO 的传输层错误 -> 人能看懂的中文短句。
std::string transportError(const GError* error) {
    if (error == nullptr) return "未知错误";

    if (error->domain == G_IO_ERROR) {
        switch (error->code) {
            case G_IO_ERROR_TIMED_OUT: return "连接超时";
            case G_IO_ERROR_HOST_NOT_FOUND: return "域名解析失败";
            case G_IO_ERROR_NETWORK_UNREACHABLE: return "网络不可达";
            case G_IO_ERROR_HOST_UNREACHABLE: return "主机不可达";
            case G_IO_ERROR_CONNECTION_REFUSED: return "连接被拒绝";
            default: break;
        }
    }
    if (error->domain == G_TLS_ERROR) {
        return std::string("TLS 握手失败（") + error->message + "）";
    }
    if (error->domain == G_RESOLVER_ERROR) {
        return "域名解析失败";
    }
    return error->message != nullptr ? error->message : "未知错误";
}

std::string failure(const std::string& reason) {
    return "网络不可达: " + reason;
}

// 从 "HTTP/1.1 200 OK" 里取状态码。
int parseStatusCode(const std::string& header) {
    const size_t space = header.find(' ');
    if (space == std::string::npos) return 0;
    return std::atoi(header.c_str() + space + 1);
}

// 大小写不敏感地取一个响应头。只扫描头部（headerBlock 不含结尾的空行）。
std::string headerValue(const std::string& headerBlock, const char* name) {
    size_t pos = headerBlock.find("\r\n");
    if (pos == std::string::npos) return std::string();
    pos += 2;

    while (pos < headerBlock.size()) {
        const size_t end = headerBlock.find("\r\n", pos);
        const std::string line =
            end == std::string::npos ? headerBlock.substr(pos) : headerBlock.substr(pos, end - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string key = trim(line.substr(0, colon));
            if (g_ascii_strcasecmp(key.c_str(), name) == 0) {
                return trim(line.substr(colon + 1));
            }
        }
        if (end == std::string::npos) break;
        pos = end + 2;
    }
    return std::string();
}

// 极简 chunked 解码。正常走 Content-Length 的话用不上，但服务端要是真发了
// chunked，不解就会把块长度当成正文。
bool dechunk(const std::string& input, std::string& out) {
    out.clear();
    size_t pos = 0;
    for (;;) {
        const size_t end = input.find("\r\n", pos);
        if (end == std::string::npos) return false;

        std::string sizeText = input.substr(pos, end - pos);
        const size_t semicolon = sizeText.find(';');   // 块扩展（;charset=...）直接忽略
        if (semicolon != std::string::npos) sizeText = sizeText.substr(0, semicolon);

        char* tail = nullptr;
        const unsigned long long size = std::strtoull(sizeText.c_str(), &tail, 16);
        if (tail == sizeText.c_str()) return false;

        pos = end + 2;
        if (size == 0) return true;   // 收尾块
        if (pos + size > input.size()) return false;
        out.append(input, pos, size);
        pos += size;
        if (input.compare(pos, 2, "\r\n") != 0) return false;
        pos += 2;
    }
}

}  // namespace

std::string cleanKey(const std::string& value) {
    std::string text = trim(value);
    while (text.size() >= 2 && text.front() == text.back() &&
           (text.front() == '"' || text.front() == '\'')) {
        text = trim(text.substr(1, text.size() - 2));
    }
    return text;
}

std::string rawEnvKey() {
    const char* value = g_getenv(kApiKeyEnv);
    return value != nullptr ? std::string(value) : std::string();
}

std::string resolveApiKey() {
    if (!trim(kApiKeyLiteral).empty()) return cleanKey(kApiKeyLiteral);
    return cleanKey(rawEnvKey());
}

std::string keySource() {
    if (!trim(kApiKeyLiteral).empty()) return "程序里的 API_KEY";
    if (!resolveApiKey().empty()) return std::string("环境变量 ") + kApiKeyEnv;
    return "没找到";
}

std::string maskKey(const std::string& key) {
    if (key.size() <= 8) return std::string(key.size(), '*');
    return key.substr(0, 4) + std::string(key.size() - 6, '*') + key.substr(key.size() - 2);
}

std::string shortError(const std::string& error) {
    const size_t colon = error.find(':');
    std::string head = trim(colon == std::string::npos ? error : error.substr(0, colon));
    if (head.empty()) head = error;
    // 必须按码点截：中文一个字 3 字节，按字节切会切出非法 UTF-8，Pango 会显示乱码。
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
    const std::string trimmed = trim(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0') return false;   // 尾部还有垃圾字符
    out = value;
    return true;
}

FetchResult fetchBalance(const std::string& apiKey) {
    FetchResult result;

    Ref<GSocketClient> client(g_socket_client_new());
    g_socket_client_set_tls(client.get(), TRUE);
    // enable-proxy 默认就是 TRUE；写出来是为了说明：它会用默认的 GProxyResolver，
    // 也就是读 http_proxy / https_proxy / all_proxy 环境变量和 GNOME 的代理设置，
    // 等价于 Windows 版的 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY。
    g_socket_client_set_enable_proxy(client.get(), TRUE);
    g_socket_client_set_timeout(client.get(), kTimeoutSeconds);

    GError* error = nullptr;
    // 必须用 connect_to_uri 而不是 connect_to_host：后者压根不查代理解析器
    // （enable-proxy 形同虚设，代理环境里会静默直连）。传 URI 才会做代理协商，
    // 走 https 代理时 GIO 会自己建 CONNECT 隧道。URI 里保留主机名而不是 IP，
    // 因为 SNI 和证书主机名校验都要靠它。
    const std::string uri = std::string("https://") + kApiHost + ":" +
                            std::to_string(443) + kApiPath;
    Ref<GSocketConnection> connection(
        g_socket_client_connect_to_uri(client.get(), uri.c_str(), 443, nullptr, &error));
    if (!connection) {
        result.error = failure(transportError(error));
        g_clear_error(&error);
        return result;
    }

    // 连接超时由 client 管，读写超时得单独设在 socket 上，免得服务端半死不活时
    // 后台线程一直挂着。
    g_socket_set_timeout(g_socket_connection_get_socket(connection.get()), kTimeoutSeconds);

    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(connection.get()));
    GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(connection.get()));

    const std::string request = "GET " + std::string(kApiPath) + " HTTP/1.1\r\n" +
                                "Host: " + std::string(kApiHost) + "\r\n" +
                                "User-Agent: DeepSeekBalanceCube/1.0\r\n" +
                                "Accept: application/json\r\n" +
                                "Authorization: Bearer " + apiKey + "\r\n" +
                                "Connection: close\r\n\r\n";

    gsize written = 0;
    if (!g_output_stream_write_all(out, request.data(), request.size(), &written, nullptr, &error)) {
        result.error = failure(transportError(error));
        g_clear_error(&error);
        return result;
    }

    // ---- 读到头部结束 ----
    std::string raw;
    char buffer[4096];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        const gssize count = g_input_stream_read(in, buffer, sizeof(buffer), nullptr, &error);
        if (count < 0) {
            result.error = failure(transportError(error));
            g_clear_error(&error);
            return result;
        }
        if (count == 0) break;   // 服务端没给完就关了，交给下面的解析去报错
        raw.append(buffer, static_cast<size_t>(count));
        if (raw.size() > 4u * 1024 * 1024) break;   // 头部不可能这么大，防跑飞
    }

    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        result.error = "返回不是合法 HTTP 响应";
        return result;
    }

    const std::string headerBlock = raw.substr(0, headerEnd);
    const unsigned int status =
        static_cast<unsigned int>(parseStatusCode(headerBlock));
    std::string body = raw.substr(headerEnd + 4);

    // ---- 按分帧方式把正文读全 ----
    const std::string encoding = headerValue(headerBlock, "Transfer-Encoding");
    const std::string lengthText = headerValue(headerBlock, "Content-Length");

    if (g_ascii_strcasecmp(encoding.c_str(), "chunked") == 0) {
        for (;;) {
            const gssize count = g_input_stream_read(in, buffer, sizeof(buffer), nullptr, &error);
            if (count < 0) {
                result.error = failure(transportError(error));
                g_clear_error(&error);
                return result;
            }
            if (count == 0) break;
            body.append(buffer, static_cast<size_t>(count));
            if (body.size() > 8u * 1024 * 1024) break;
        }
        std::string decoded;
        if (!dechunk(body, decoded)) {
            result.error = "返回不是合法 HTTP 响应";
            return result;
        }
        body = std::move(decoded);
    } else {
        const size_t expected =
            lengthText.empty() ? 0 : static_cast<size_t>(std::strtoull(lengthText.c_str(), nullptr, 10));
        while (body.size() < expected) {
            const gssize count = g_input_stream_read(in, buffer, sizeof(buffer), nullptr, &error);
            if (count <= 0) break;
            body.append(buffer, static_cast<size_t>(count));
        }
    }

    if (status < 200 || status >= 300) {
        result.error = httpFailure(status, body);
        return result;
    }

    json::Value document;
    if (!json::parse(body, document)) {
        result.error = "返回不是合法 JSON";
        return result;
    }

    result.ok = true;
    result.payload = std::move(body);
    return result;
}

const char* demoPayload() {
    return kDemoPayload;
}
