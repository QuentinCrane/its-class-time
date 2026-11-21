#include "classtime/schedule.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace classtime {
namespace {

bool IsValidDate(int year, int month, int day) {
  if (year < 1900 || year > 9999) return false;
  if (month < 1 || month > 12) return false;
  static const int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int limit = kDaysInMonth[month - 1];
  const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (month == 2 && leap) limit = 29;
  return day >= 1 && day <= limit;
}

// Howard Hinnant 的 days_from_civil：返回相对 1970-01-01 的天数。
long long DaysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153u * (month > 2 ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return static_cast<long long>(era) * 146097LL + static_cast<long long>(doe) - 719468LL;
}

}  // namespace

const char* SegmentKindToString(SegmentKind kind) noexcept {
  switch (kind) {
    case SegmentKind::Focus: return "focus";
    case SegmentKind::Break: return "break";
    case SegmentKind::LongBreak: return "long_break";
    case SegmentKind::Lunch: return "lunch";
  }
  return "focus";
}

std::optional<SegmentKind> SegmentKindFromString(std::string_view text) noexcept {
  if (text == "focus") return SegmentKind::Focus;
  if (text == "break") return SegmentKind::Break;
  if (text == "long_break") return SegmentKind::LongBreak;
  if (text == "lunch") return SegmentKind::Lunch;
  return std::nullopt;
}

bool IsLongRest(SegmentKind kind) noexcept {
  return kind == SegmentKind::Lunch || kind == SegmentKind::LongBreak;
}

bool IsRest(SegmentKind kind) noexcept {
  return kind == SegmentKind::Break || kind == SegmentKind::LongBreak || kind == SegmentKind::Lunch;
}

std::optional<TimeOfDay> TimeOfDay::FromTotalMinutes(int total) noexcept {
  if (total < 0 || total >= 24 * 60) return std::nullopt;
  TimeOfDay value;
  value.hour = total / 60;
  value.minute = total % 60;
  return value;
}

std::optional<TimeOfDay> TimeOfDay::Parse(std::string_view text) noexcept {
  if (text.size() != 5 || text[2] != ':') return std::nullopt;
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!digit(text[0]) || !digit(text[1]) || !digit(text[3]) || !digit(text[4])) return std::nullopt;
  const int hour = (text[0] - '0') * 10 + (text[1] - '0');
  const int minute = (text[3] - '0') * 10 + (text[4] - '0');
  if (hour > 23 || minute > 59) return std::nullopt;
  TimeOfDay value;
  value.hour = hour;
  value.minute = minute;
  return value;
}

std::string TimeOfDay::ToString() const {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
  return std::string(buffer);
}

bool operator==(const TimeOfDay& lhs, const TimeOfDay& rhs) noexcept {
  return lhs.hour == rhs.hour && lhs.minute == rhs.minute;
}

bool operator!=(const TimeOfDay& lhs, const TimeOfDay& rhs) noexcept { return !(lhs == rhs); }

bool operator<(const TimeOfDay& lhs, const TimeOfDay& rhs) noexcept {
  return lhs.TotalMinutes() < rhs.TotalMinutes();
}

bool operator==(const Segment& lhs, const Segment& rhs) noexcept {
  return lhs.id == rhs.id && lhs.label == rhs.label && lhs.kind == rhs.kind &&
         lhs.start == rhs.start && lhs.end == rhs.end;
}

bool operator!=(const Segment& lhs, const Segment& rhs) noexcept { return !(lhs == rhs); }

bool Schedule::IsActiveOn(int weekday) const noexcept {
  return std::find(activeWeekdays.begin(), activeWeekdays.end(), weekday) != activeWeekdays.end();
}

int WallClock::Weekday() const noexcept {
  const long long days = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  // 1970-01-01 是周四，故加 3 后取模得到 ISO 星期（1=周一）。
  return static_cast<int>(((days % 7) + 7 + 3) % 7) + 1;
}

std::string WallClock::DateKey() const {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  return std::string(buffer);
}

std::string WallClock::IsoString() const {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour,
                minute, second);
  return std::string(buffer);
}

std::optional<WallClock> WallClock::ParseIso(std::string_view text) {
  // 接受 YYYY-MM-DDTHH:MM:SS，长度固定 19。
  if (text.size() != 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
      text[16] != ':') {
    return std::nullopt;
  }
  const auto number = [&text](size_t offset, size_t length) -> std::optional<int> {
    int value = 0;
    for (size_t i = 0; i < length; ++i) {
      const char c = text[offset + i];
      if (c < '0' || c > '9') return std::nullopt;
      value = value * 10 + (c - '0');
    }
    return value;
  };
  const auto year = number(0, 4);
  const auto month = number(5, 2);
  const auto day = number(8, 2);
  const auto hour = number(11, 2);
  const auto minute = number(14, 2);
  const auto second = number(17, 2);
  if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;
  if (!IsValidDate(*year, *month, *day)) return std::nullopt;
  if (*hour > 23 || *minute > 59 || *second > 59) return std::nullopt;
  WallClock value;
  value.year = *year;
  value.month = *month;
  value.day = *day;
  value.hour = *hour;
  value.minute = *minute;
  value.second = *second;
  return value;
}

bool ValidateSegments(const std::vector<Segment>& segments, std::vector<ValidationIssue>* issues) {
  std::vector<ValidationIssue> local;
  std::set<std::string> ids;

  for (const auto& segment : segments) {
    if (segment.id.empty()) {
      local.push_back({segment.id, "时段缺少稳定 ID"});
    } else if (!ids.insert(segment.id).second) {
      local.push_back({segment.id, "时段 ID 重复"});
    }
    if (segment.label.empty()) {
      local.push_back({segment.id, "名称不能为空"});
    }
    if (segment.start.TotalMinutes() >= segment.end.TotalMinutes()) {
      local.push_back({segment.id, "结束时间必须晚于开始时间"});
    }
  }

  auto sorted = SortedByStart(segments);
  for (size_t i = 1; i < sorted.size(); ++i) {
    if (sorted[i].start.TotalMinutes() < sorted[i - 1].end.TotalMinutes()) {
      local.push_back({sorted[i].id, "与上一时段重叠（" + sorted[i - 1].label + "）"});
    }
  }

  if (issues != nullptr) {
    *issues = local;
  }
  return local.empty();
}

bool ValidateSchedule(const Schedule& schedule, std::vector<ValidationIssue>* issues) {
  std::vector<ValidationIssue> local;
  if (schedule.id.empty()) local.push_back({"", "模板 ID 不能为空"});
  if (schedule.name.empty()) local.push_back({"", "模板名称不能为空"});
  for (const int weekday : schedule.activeWeekdays) {
    if (weekday < 1 || weekday > 7) local.push_back({"", "星期取值必须在 1–7"});
  }
  std::vector<ValidationIssue> segmentIssues;
  ValidateSegments(schedule.segments, &segmentIssues);
  local.insert(local.end(), segmentIssues.begin(), segmentIssues.end());
  if (issues != nullptr) *issues = local;
  return local.empty();
}

std::vector<Segment> SortedByStart(const std::vector<Segment>& segments) {
  std::vector<Segment> sorted = segments;
  std::stable_sort(sorted.begin(), sorted.end(), [](const Segment& lhs, const Segment& rhs) {
    return lhs.start.TotalMinutes() < rhs.start.TotalMinutes();
  });
  return sorted;
}

const Segment* FindSegmentById(const std::vector<Segment>& segments, std::string_view id) noexcept {
  for (const auto& segment : segments) {
    if (segment.id == id) return &segment;
  }
  return nullptr;
}

}  // namespace classtime
