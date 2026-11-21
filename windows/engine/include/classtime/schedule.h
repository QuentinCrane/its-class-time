// 作息数据模型：与 shared/schemas/schedule.schema.json 一一对应。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace classtime {

enum class SegmentKind { Focus, Break, LongBreak, Lunch };

const char* SegmentKindToString(SegmentKind kind) noexcept;
std::optional<SegmentKind> SegmentKindFromString(std::string_view text) noexcept;

// 午休与长休息是“分块分隔符”，短课间不是。
bool IsLongRest(SegmentKind kind) noexcept;
bool IsRest(SegmentKind kind) noexcept;

struct TimeOfDay {
  int hour = 0;
  int minute = 0;

  int TotalMinutes() const noexcept { return hour * 60 + minute; }
  static std::optional<TimeOfDay> FromTotalMinutes(int total) noexcept;
  static std::optional<TimeOfDay> Parse(std::string_view text) noexcept;
  std::string ToString() const;
};

bool operator==(const TimeOfDay& lhs, const TimeOfDay& rhs) noexcept;
bool operator!=(const TimeOfDay& lhs, const TimeOfDay& rhs) noexcept;
bool operator<(const TimeOfDay& lhs, const TimeOfDay& rhs) noexcept;

struct Segment {
  std::string id;
  std::string label;
  SegmentKind kind = SegmentKind::Focus;
  TimeOfDay start;
  TimeOfDay end;

  int DurationMinutes() const noexcept { return end.TotalMinutes() - start.TotalMinutes(); }
};

bool operator==(const Segment& lhs, const Segment& rhs) noexcept;
bool operator!=(const Segment& lhs, const Segment& rhs) noexcept;

struct Schedule {
  int schemaVersion = 1;
  std::string id;
  std::string name;
  std::vector<int> activeWeekdays;  // 1=周一 … 7=周日
  std::vector<Segment> segments;

  bool IsActiveOn(int weekday) const noexcept;
};

// 本地墙上时间。引擎不依赖系统时区数据库，跨日/夏令时由调用方重新定位。
struct WallClock {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;

  int Weekday() const noexcept;  // 1=周一 … 7=周日
  int MinutesSinceMidnight() const noexcept { return hour * 60 + minute; }
  std::string DateKey() const;    // YYYY-MM-DD
  std::string IsoString() const;  // YYYY-MM-DDTHH:MM:SS
  static std::optional<WallClock> ParseIso(std::string_view text);
};

struct ValidationIssue {
  std::string segmentId;
  std::string message;
};

// 校验：id 唯一、名称非空、00:00–23:59、结束晚于开始、不存在重叠（相邻允许）。
bool ValidateSegments(const std::vector<Segment>& segments, std::vector<ValidationIssue>* issues);
bool ValidateSchedule(const Schedule& schedule, std::vector<ValidationIssue>* issues);

// 按开始时间稳定排序；同一开始时间保持输入顺序。
std::vector<Segment> SortedByStart(const std::vector<Segment>& segments);

const Segment* FindSegmentById(const std::vector<Segment>& segments, std::string_view id) noexcept;

}  // namespace classtime
