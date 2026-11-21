// 极简 JSON 读写：只依赖标准库，供作息引擎与配置存储共用。
// 之所以不引入第三方库：字段很有限，工程希望保持零外部依赖与可测试性。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace classtime {
namespace json {

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Value() = default;

  static Value MakeBool(bool value);
  static Value MakeNumber(double value);
  static Value MakeString(std::string value);
  static Value MakeArray();
  static Value MakeObject();

  Type type() const noexcept { return type_; }
  bool IsNull() const noexcept { return type_ == Type::Null; }
  bool IsBool() const noexcept { return type_ == Type::Bool; }
  bool IsNumber() const noexcept { return type_ == Type::Number; }
  bool IsString() const noexcept { return type_ == Type::String; }
  bool IsArray() const noexcept { return type_ == Type::Array; }
  bool IsObject() const noexcept { return type_ == Type::Object; }

  bool AsBool(bool fallback = false) const noexcept;
  double AsNumber(double fallback = 0.0) const noexcept;
  int AsInt(int fallback = 0) const noexcept;
  std::string AsString(std::string_view fallback = {}) const;

  // 数组写入。
  void Append(Value value);
  const std::vector<Value>& items() const noexcept { return items_; }

  // 对象写入；重复键会覆盖旧值。
  void Set(std::string key, Value value);
  const Value* Find(std::string_view key) const noexcept;
  const std::vector<std::pair<std::string, Value>>& members() const noexcept { return members_; }

  // 解析失败时返回空，error 填具体位置与原因。
  static std::optional<Value> Parse(std::string_view text, std::string* error = nullptr);
  std::string Dump(int indent = 2) const;

 private:
  void DumpTo(std::string& out, int indent, int depth) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Value> items_;
  std::vector<std::pair<std::string, Value>> members_;
};

// UTF-8 与 UTF-16 互转（Windows 展示层/JSON 交互需要）。
std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

}  // namespace json
}  // namespace classtime
