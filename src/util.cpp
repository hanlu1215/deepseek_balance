#include "util.h"

#include <windows.h>

namespace {

bool isSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v';
}

bool isHighSurrogate(wchar_t c) {
    return c >= 0xD800 && c <= 0xDBFF;
}

}  // namespace

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int need = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring trimWide(const std::wstring& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::string trimNarrow(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    while (begin < end && space(text[begin])) ++begin;
    while (end > begin && space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::wstring truncateChars(const std::wstring& text, size_t count) {
    if (text.size() <= count) return text;
    size_t cut = count;
    // 别停在半个代理对上
    if (cut > 0 && isHighSurrogate(text[cut - 1])) --cut;
    return text.substr(0, cut);
}

std::wstring executableDir() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return std::wstring();
        if (written < buffer.size()) {
            buffer.resize(written);
            break;
        }
        buffer.resize(buffer.size() * 2);   // 路径比 MAX_PATH 长，翻倍再来
    }
    const size_t slash = buffer.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return buffer.substr(0, slash + 1);
}

std::wstring win32ErrorMessage(unsigned long code) {
    wchar_t* raw = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&raw), 0, nullptr);

    std::wstring message;
    if (length > 0 && raw != nullptr) {
        message.assign(raw, length);
        LocalFree(raw);
    } else {
        message = L"错误码 " + std::to_wstring(code);
    }
    return trimWide(message);
}
