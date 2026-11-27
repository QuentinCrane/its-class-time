// 时间引擎：只做“给定作息与当下墙上时间 → 状态/进度/下一边界”的确定性计算。
// 不持有定时器、不读系统时钟，便于用 shared/test-cases 做对等测试。
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "classtime/schedule.h"

namespace classtime {

enum class DayState { Idle, Focus, Break, LongBreak, Lunch };

const char* DayStateToString(DayState state) noexcept;

enum class BoundaryKind {
  FocusStart,
  FocusEnd,
  BreakStart,
  BreakEnd,
  LongBreakStart,
  LongBreakEnd,
  LunchStart,
  LunchEnd,
};

const char* BoundaryKindToString(BoundaryKind kind) noexcept;

BoundaryKind StartBoundaryKind(SegmentKind kind) noexcept;
BoundaryKind EndBoundaryKind(SegmentKind kind) noexcept;

// 值语义的时段快照，避免 UI 持有引擎内部 vector 的指针。
struct SegmentRef {
  std::string id;
  std::string label;
  SegmentKind kind = SegmentKind::Focus;
  TimeOfDay start;
  TimeOfDay end;
  int focusOrdinal = 0;  // 仅 focus 有效，1 起；休息为 0。

  int DurationMinutes() const noexcept { return end.TotalMinutes() - start.TotalMinutes(); }
};

struct Snapshot {
  bool hasSchedule = false;
  DayState state = DayState::Idle;
  std::optional<SegmentRef> current;
  std::optional<SegmentRef> next;  // 下一个时段（含休息），没有则为空
  double progress = 0.0;           // 当前时段进度，0–1；无当前时段为 0
  std::optional<WallClock> nextBoundary;
  std::optional<BoundaryKind> nextBoundaryKind;
};

Snapshot Evaluate(const std::vector<Segment>& segments, const WallClock& now);

struct BoundaryEvent {
  BoundaryKind kind = BoundaryKind::FocusStart;
  SegmentRef segment;
  WallClock at;
};

// 返回恰好发生在 now（精确到秒）的边界事件，最多两个（一段结束 + 下一段开始）。
std::vector<BoundaryEvent> EventsAt(const std::vector<Segment>& segments, const WallClock& now);

// 去重键：日期 + 段 ID + 边界类型 + 计划修订号。
std::string EventKey(const std::string& dateKey, const std::string& segmentId, BoundaryKind kind,
                     int planRevision);

// —— 显示范围（仅影响展示过滤，不影响通知与引擎判定）——
enum class Scope { AllDay, CurrentBlock };

// 以 lunch / long_break 为分块分隔符，返回原始 segments 的下标，结果按开始时间排列。
// 当前处于长休息 → 同时展示休息和即将到来的块；没有下一块则展示上一块。
std::vector<size_t> VisibleSegmentIndices(const std::vector<Segment>& segments, const WallClock& now,
                                          Scope scope);

// 当前块的时间范围，用于 hover/概览；没有块时返回空。
struct BlockBounds {
  TimeOfDay begin;
  TimeOfDay end;
  size_t firstSortedIndex = 0;
  size_t lastSortedIndex = 0;
};
std::optional<BlockBounds> CurrentBlockBounds(const std::vector<Segment>& segments,
                                              const WallClock& now);

int FocusCount(const std::vector<Segment>& segments);

}  // namespace classtime
