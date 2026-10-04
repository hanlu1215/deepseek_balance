#pragma once

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// 一个够用就好的 JSON 解析器：只需要读懂 DeepSeek 余额接口那点结构。
// 字符串统一按 UTF-8 存，显示之前再转宽字符。
namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;                                  // UTF-8
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;   // 保持出现顺序

    const Value* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& entry : object) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }

    const Value* at(size_t index) const {
        if (type != Type::Array || index >= array.size()) return nullptr;
        return &array[index];
    }

    // 取字符串；是数字就顺手转成文本（接口偶尔会换类型）。
    std::string asString(const std::string& fallback) const {
        if (type == Type::String) return string;
        if (type == Type::Number) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.6g", number);
            return buffer;
        }
        return fallback;
    }
};

// 解析整段文本；尾部有多余字符也算失败（和 json.loads 的行为一致）。
bool parse(const std::string& text, Value& out);

}  // namespace json
