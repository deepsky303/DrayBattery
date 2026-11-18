#pragma once
// Small bounded JSON parser for localhost Logitech G HUB protocol replies.
// No runtime dependency; no evaluation or dynamic execution.
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace ghubjson {
struct Value {
    enum Type { Null, Boolean, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Value> array;
    std::map<std::string, Value> object;
    const Value* get(const std::string& key) const {
        if (type != Object) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
    std::string str() const { return type == String ? text : std::string(); }
    int integer(int fallback = -1) const {
        return type == Number && number >= 0 && number <= 100000 ? (int)number : fallback;
    }
};

class Parser {
    const char* cur_;
    const char* end_;
    void ws() {
        while (cur_ < end_ && (*cur_ == ' ' || *cur_ == '\n' || *cur_ == '\t' || *cur_ == '\r')) ++cur_;
    }
    bool literal(const char* word) {
        const char* start = word;
        while (*word && cur_ < end_ && *cur_ == *word) { ++cur_; ++word; }
        return *word == 0 && cur_ != end_ && start != word;
    }
    bool string(std::string& value) {
        if (cur_ >= end_ || *cur_++ != '"') return false;
        while (cur_ < end_) {
            unsigned char c = (unsigned char)*cur_++;
            if (c == '"') return true;
            if (c < 32) return false;
            if (c != '\\') { value.push_back((char)c); continue; }
            if (cur_ >= end_) return false;
            c = (unsigned char)*cur_++;
            switch (c) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'u':
                    if (end_ - cur_ < 4) return false;
                    for (int k = 0; k < 4; ++k)
                        if (!std::isxdigit((unsigned char)cur_[k])) return false;
                    cur_ += 4;
                    value.push_back('?'); // G HUB's hardware names and paths use ASCII.
                    break;
                default: return false;
            }
        }
        return false;
    }
    bool value(Value& v, int depth) {
        if (depth > 40) return false;
        ws();
        if (cur_ >= end_) return false;
        if (*cur_ == '{') {
            v.type = Value::Object;
            ++cur_;
            ws();
            if (cur_ < end_ && *cur_ == '}') { ++cur_; return true; }
            while (cur_ < end_) {
                std::string key;
                if (!string(key)) return false;
                ws();
                if (cur_ >= end_ || *cur_++ != ':') return false;
                if (!value(v.object[key], depth + 1)) return false;
                ws();
                if (cur_ >= end_) return false;
                if (*cur_ == '}') { ++cur_; return true; }
                if (*cur_++ != ',') return false;
                ws();
            }
            return false;
        }
        if (*cur_ == '[') {
            v.type = Value::Array;
            ++cur_;
            ws();
            if (cur_ < end_ && *cur_ == ']') { ++cur_; return true; }
            while (cur_ < end_) {
                v.array.emplace_back();
                if (!value(v.array.back(), depth + 1)) return false;
                ws();
                if (cur_ >= end_) return false;
                if (*cur_ == ']') { ++cur_; return true; }
                if (*cur_++ != ',') return false;
            }
            return false;
        }
        if (*cur_ == '"') {
            v.type = Value::String;
            return string(v.text);
        }
        if (*cur_ == 't') {
            if (end_ - cur_ < 4 || std::string(cur_, 4) != "true") return false;
            cur_ += 4; v.type = Value::Boolean; v.boolean = true; return true;
        }
        if (*cur_ == 'f') {
            if (end_ - cur_ < 5 || std::string(cur_, 5) != "false") return false;
            cur_ += 5; v.type = Value::Boolean; v.boolean = false; return true;
        }
        if (*cur_ == 'n') {
            if (end_ - cur_ < 4 || std::string(cur_, 4) != "null") return false;
            cur_ += 4; v.type = Value::Null; return true;
        }
        char* next = nullptr;
        v.number = std::strtod(cur_, &next);
        if (next == cur_ || next > end_) return false;
        cur_ = next;
        v.type = Value::Number;
        return true;
    }
public:
    explicit Parser(const std::string& input) : cur_(input.data()), end_(input.data() + input.size()) {}
    bool parse(Value& v) { return value(v, 0) && (ws(), cur_ == end_); }
};
} // namespace ghubjson
