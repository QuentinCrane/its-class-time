#include "classtime/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace classtime {
namespace json {
namespace {

constexpr int kMaxDepth = 32;

void AppendUtf8(std::string& out, unsigned int cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

class Parser {
 public:
  Parser(std::string_view text, std::string* error) : text_(text), error_(error) {}

  bool ParseDocument(Value& out) {
    SkipWhitespace();
    if (!ParseValue(out, 0)) return false;
    SkipWhitespace();
    if (pos_ != text_.size()) {
      Fail("文档结尾存在多余内容");
      return false;
    }
    return true;
  }

 private:
  void Fail(const char* reason) {
    if (error_ != nullptr && error_->empty()) {
      *error_ = std::string(reason) + "（偏移 " + std::to_string(pos_) + "）";
    }
  }

  void SkipWhitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool Consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool ParseValue(Value& out, int depth) {
    if (depth > kMaxDepth) {
      Fail("嵌套层级过深");
      return false;
    }
    if (pos_ >= text_.size()) {
      Fail("意外的结尾");
      return false;
    }
    const char c = text_[pos_];
    switch (c) {
      case '{':
        return ParseObject(out, depth);
      case '[':
        return ParseArray(out, depth);
      case '"': {
        std::string str;
        if (!ParseString(str)) return false;
        out = Value::MakeString(std::move(str));
        return true;
      }
      case 't':
        if (text_.compare(pos_, 4, "true") == 0) {
          pos_ += 4;
          out = Value::MakeBool(true);
          return true;
        }
        Fail("无效的字面量");
        return false;
      case 'f':
        if (text_.compare(pos_, 5, "false") == 0) {
          pos_ += 5;
          out = Value::MakeBool(false);
          return true;
        }
        Fail("无效的字面量");
        return false;
      case 'n':
        if (text_.compare(pos_, 4, "null") == 0) {
          pos_ += 4;
          out = Value();
          return true;
        }
        Fail("无效的字面量");
        return false;
      default:
        return ParseNumber(out);
    }
  }

  bool ParseObject(Value& out, int depth) {
    if (!Consume('{')) return false;
    Value object = Value::MakeObject();
    SkipWhitespace();
    if (Consume('}')) {
      out = std::move(object);
      return true;
    }
    while (true) {
      SkipWhitespace();
      std::string key;
      if (!ParseString(key)) return false;
      SkipWhitespace();
      if (!Consume(':')) {
        Fail("对象缺少冒号");
        return false;
      }
      SkipWhitespace();
      Value value;
      if (!ParseValue(value, depth + 1)) return false;
      object.Set(std::move(key), std::move(value));
      SkipWhitespace();
      if (Consume(',')) continue;
      if (Consume('}')) break;
      Fail("对象缺少逗号或右花括号");
      return false;
    }
    out = std::move(object);
    return true;
  }

  bool ParseArray(Value& out, int depth) {
    if (!Consume('[')) return false;
    Value array = Value::MakeArray();
    SkipWhitespace();
    if (Consume(']')) {
      out = std::move(array);
      return true;
    }
    while (true) {
      SkipWhitespace();
      Value value;
      if (!ParseValue(value, depth + 1)) return false;
      array.Append(std::move(value));
      SkipWhitespace();
      if (Consume(',')) continue;
      if (Consume(']')) break;
      Fail("数组缺少逗号或右方括号");
      return false;
    }
    out = std::move(array);
    return true;
  }

  bool ParseString(std::string& out) {
    if (!Consume('"')) {
      Fail("期望字符串");
      return false;
    }
    out.clear();
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c == '\\') {
        ++pos_;
        if (pos_ >= text_.size()) break;
        const char esc = text_[pos_++];
        switch (esc) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            unsigned int cp = 0;
            if (!ReadHex4(cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              // UTF-16 代理对
              if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                Fail("缺少低代理项");
                return false;
              }
              pos_ += 2;
              unsigned int low = 0;
              if (!ReadHex4(low)) return false;
              if (low < 0xDC00 || low > 0xDFFF) {
                Fail("无效的低代理项");
                return false;
              }
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              Fail("孤立的低代理项");
              return false;
            }
            AppendUtf8(out, cp);
            break;
          }
          default:
            Fail("未知转义序列");
            return false;
        }
        continue;
      }
      if (c < 0x20) {
        Fail("字符串中含未转义控制字符");
        return false;
      }
      out.push_back(static_cast<char>(c));
      ++pos_;
    }
    Fail("字符串未闭合");
    return false;
  }

  bool ReadHex4(unsigned int& value) {
    if (pos_ + 4 > text_.size()) {
      Fail("\\u 转义不完整");
      return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = HexDigit(text_[pos_ + i]);
      if (digit < 0) {
        Fail("\\u 转义含非法字符");
        return false;
      }
      value = (value << 4) | static_cast<unsigned int>(digit);
    }
    pos_ += 4;
    return true;
  }

  bool ParseNumber(Value& out) {
    const size_t begin = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
    bool digits = false;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
      digits = true;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
        digits = true;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
    }
    if (!digits) {
      Fail("无效的数字");
      return false;
    }
    const std::string token(text_.substr(begin, pos_ - begin));
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || (end != nullptr && *end != '\0')) {
      Fail("无效的数字");
      return false;
    }
    out = Value::MakeNumber(value);
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
  std::string* error_ = nullptr;
};

void DumpEscaped(std::string& out, std::string_view text) {
  out.push_back('"');
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(c));
          out += buffer;
        } else {
          out.push_back(raw);
        }
    }
  }
  out.push_back('"');
}

}  // namespace

Value Value::MakeBool(bool value) {
  Value v;
  v.type_ = Type::Bool;
  v.bool_ = value;
  return v;
}

Value Value::MakeNumber(double value) {
  Value v;
  v.type_ = Type::Number;
  v.number_ = value;
  return v;
}

Value Value::MakeString(std::string value) {
  Value v;
  v.type_ = Type::String;
  v.string_ = std::move(value);
  return v;
}

Value Value::MakeArray() {
  Value v;
  v.type_ = Type::Array;
  return v;
}

Value Value::MakeObject() {
  Value v;
  v.type_ = Type::Object;
  return v;
}

bool Value::AsBool(bool fallback) const noexcept {
  return type_ == Type::Bool ? bool_ : fallback;
}

double Value::AsNumber(double fallback) const noexcept {
  return type_ == Type::Number ? number_ : fallback;
}

int Value::AsInt(int fallback) const noexcept {
  if (type_ != Type::Number) return fallback;
  if (number_ < -2147483648.0 || number_ > 2147483647.0) return fallback;
  return static_cast<int>(number_);
}

std::string Value::AsString(std::string_view fallback) const {
  if (type_ != Type::String) return std::string(fallback);
  return string_;
}

void Value::Append(Value value) {
  if (type_ == Type::Array) items_.push_back(std::move(value));
}

void Value::Set(std::string key, Value value) {
  if (type_ != Type::Object) return;
  for (auto& member : members_) {
    if (member.first == key) {
      member.second = std::move(value);
      return;
    }
  }
  members_.emplace_back(std::move(key), std::move(value));
}

const Value* Value::Find(std::string_view key) const noexcept {
  if (type_ != Type::Object) return nullptr;
  for (const auto& member : members_) {
    if (member.first == key) return &member.second;
  }
  return nullptr;
}

std::optional<Value> Value::Parse(std::string_view text, std::string* error) {
  if (error != nullptr) error->clear();
  Parser parser(text, error);
  Value value;
  if (!parser.ParseDocument(value)) return std::nullopt;
  return value;
}

std::string Value::Dump(int indent) const {
  std::string out;
  DumpTo(out, indent, 0);
  return out;
}

void Value::DumpTo(std::string& out, int indent, int depth) const {
  const std::string pad(static_cast<size_t>(indent) * static_cast<size_t>(depth), ' ');
  const std::string padInner(static_cast<size_t>(indent) * static_cast<size_t>(depth + 1), ' ');
  switch (type_) {
    case Type::Null:
      out += "null";
      return;
    case Type::Bool:
      out += bool_ ? "true" : "false";
      return;
    case Type::Number: {
      char buffer[64];
      if (number_ == static_cast<double>(static_cast<long long>(number_))) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number_));
      } else {
        std::snprintf(buffer, sizeof(buffer), "%.10g", number_);
      }
      out += buffer;
      return;
    }
    case Type::String:
      DumpEscaped(out, string_);
      return;
    case Type::Array:
      if (items_.empty()) {
        out += "[]";
        return;
      }
      out += "[\n";
      for (size_t i = 0; i < items_.size(); ++i) {
        out += padInner;
        items_[i].DumpTo(out, indent, depth + 1);
        out += (i + 1 < items_.size()) ? ",\n" : "\n";
      }
      out += pad + "]";
      return;
    case Type::Object:
      if (members_.empty()) {
        out += "{}";
        return;
      }
      out += "{\n";
      for (size_t i = 0; i < members_.size(); ++i) {
        out += padInner;
        DumpEscaped(out, members_[i].first);
        out += ": ";
        members_[i].second.DumpTo(out, indent, depth + 1);
        out += (i + 1 < members_.size()) ? ",\n" : "\n";
      }
      out += pad + "}";
      return;
  }
}

std::wstring Utf8ToWide(std::string_view text) {
  std::wstring out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    unsigned int cp = 0xFFFD;
    size_t length = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
      cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
      length = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
      cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
      length = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
      cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
           ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
      length = 4;
    }
    i += length;
    if (cp <= 0xFFFF) {
      out.push_back(static_cast<wchar_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

std::string WideToUtf8(std::wstring_view text) {
  std::string out;
  out.reserve(text.size() * 3 / 2);
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned int cp = static_cast<unsigned int>(text[i]);
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
      const unsigned int low = static_cast<unsigned int>(text[i + 1]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      }
    }
    AppendUtf8(out, cp);
  }
  return out;
}

}  // namespace json
}  // namespace classtime
