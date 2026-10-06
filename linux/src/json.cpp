#include "json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {
namespace {

void appendUtf8(std::string& out, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        out += static_cast<char>(codePoint);
    } else if (codePoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parseDocument(Value& out) {
        skipWhitespace();
        if (!parseValue(out)) return false;
        skipWhitespace();
        return pos_ >= text_.size();
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool matchWord(const char* word) {
        const size_t length = std::strlen(word);
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        return true;
    }

    bool parseValue(Value& out) {
        if (atEnd()) return false;
        switch (peek()) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"':
                out.type = Value::Type::String;
                return parseString(out.string);
            case 't':
                if (!matchWord("true")) return false;
                out.type = Value::Type::Bool;
                out.boolean = true;
                return true;
            case 'f':
                if (!matchWord("false")) return false;
                out.type = Value::Type::Bool;
                out.boolean = false;
                return true;
            case 'n':
                if (!matchWord("null")) return false;
                out.type = Value::Type::Null;
                return true;
            default:
                return parseNumber(out);
        }
    }

    bool parseNumber(Value& out) {
        const size_t start = pos_;
        if (!atEnd() && (peek() == '-' || peek() == '+')) ++pos_;

        bool anyDigit = false;
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            ++pos_;
            anyDigit = true;
        }
        if (!atEnd() && peek() == '.') {
            ++pos_;
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
                anyDigit = true;
            }
        }
        if (!anyDigit) return false;

        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!atEnd() && (peek() == '-' || peek() == '+')) ++pos_;
            bool anyExponent = false;
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
                anyExponent = true;
            }
            if (!anyExponent) return false;
        }

        out.type = Value::Type::Number;
        out.number = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
        return true;
    }

    bool readHex4(unsigned int& value) {
        if (pos_ + 4 > text_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out) {
        out.clear();
        if (atEnd() || peek() != '"') return false;
        ++pos_;

        while (!atEnd()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;

            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }

            if (atEnd()) return false;
            const char escape = text_[pos_++];
            switch (escape) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned int codePoint = 0;
                    if (!readHex4(codePoint)) return false;
                    // 代理对：高位后面跟着 \uDC00-\uDFFF 就合成一个码点
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF &&
                        pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        const size_t save = pos_;
                        pos_ += 2;
                        unsigned int low = 0;
                        if (readHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save;   // 不是合法的低位，退回去当独立码点处理
                        }
                    }
                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool parseArray(Value& out) {
        out.type = Value::Type::Array;
        ++pos_;   // '['
        skipWhitespace();
        if (!atEnd() && peek() == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            Value child;
            if (!parseValue(child)) return false;
            out.array.push_back(std::move(child));
            skipWhitespace();
            if (atEnd()) return false;
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool parseObject(Value& out) {
        out.type = Value::Type::Object;
        ++pos_;   // '{'
        skipWhitespace();
        if (!atEnd() && peek() == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (atEnd() || peek() != ':') return false;
            ++pos_;
            skipWhitespace();
            Value child;
            if (!parseValue(child)) return false;
            out.object.emplace_back(std::move(key), std::move(child));
            skipWhitespace();
            if (atEnd()) return false;
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }
};

}  // namespace

bool parse(const std::string& text, Value& out) {
    Parser parser(text);
    return parser.parseDocument(out);
}

}  // namespace json
