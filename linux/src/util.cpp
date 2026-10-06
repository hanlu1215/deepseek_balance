#include "util.h"

#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <vector>

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// UTF-8 序列的字节数；非法首字节返回 1（当作单字节往前走，不至于卡死）。
size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

}  // namespace

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::string truncateChars(const std::string& text, size_t count) {
    size_t pos = 0;
    size_t seen = 0;
    while (pos < text.size() && seen < count) {
        pos += sequenceLength(static_cast<unsigned char>(text[pos]));
        ++seen;
    }
    // pos 可能越过末尾（末尾是半个多字节序列），夹一下保证 substr 合法
    if (pos >= text.size()) return text;
    return text.substr(0, pos);
}

size_t charCount(const std::string& text) {
    size_t pos = 0;
    size_t seen = 0;
    while (pos < text.size()) {
        pos += sequenceLength(static_cast<unsigned char>(text[pos]));
        ++seen;
    }
    return seen;
}

std::string executableDir() {
    // /proc/self/exe 是个软链接，指向真正的可执行文件；readlink 不会自动加结尾 '\0'。
    std::vector<char> buffer(PATH_MAX, '\0');
    const ssize_t written = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (written <= 0) return std::string();
    buffer[static_cast<size_t>(written)] = '\0';

    const std::string path(buffer.data(), static_cast<size_t>(written));
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return std::string();
    return path.substr(0, slash + 1);
}
