#include "classtime/time_engine.h"

#include <algorithm>
#include <limits>

namespace classtime {
namespace {

// 排序后的索引（按开始时间稳定排序），供内部计算与 UI 共用。
std::vector<size_t> SortedIndices(const std::vector<Segment>& segments) {
  std::vector<size_t> indices(segments.size());
  for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
  std::stable_sort(indices.begin(), indices.end(), [&segments](size_t lhs, size_t rhs) {
    return segments[lhs].start.TotalMinutes() < segments[rhs].start.TotalMinutes();
  });
  return indices;
}

int SecondsOfDay(const WallClock& clock) {
  return clock.hour * 3600 + clock.minute * 60 + clock.second;
}

SegmentRef MakeRef(const Segment& segment, int focusOrdinal) {
  SegmentRef ref;
  ref.id = segment.id;
  ref.label = segment.label;
  ref.kind = segment.kind;
  ref.start = segment.start;
  ref.end = segment.end;
  ref.focusOrdinal = focusOrdinal;
  return ref;
}

DayState StateOf(SegmentKind kind) {
  switch (kind) {
    case SegmentKind::Focus: return DayState::Focus;
    case SegmentKind::Break: return DayState::Break;
    case SegmentKind::LongBreak: return DayState::LongBreak;
    case SegmentKind::Lunch: return DayState::Lunch;
  }
  return DayState::Idle;
}

struct Block {
  std::vector<size_t> members;  // 排序后下标
  int beginMinutes = 0;
  int endMinutes = 0;
};

std::vector<Block> SplitBlocks(const std::vector<Segment>& segments,
                               const std::vector<size_t>& sorted) {
  std::vector<Block> blocks;
  Block current;
  for (const size_t index : sorted) {
    const Segment& segment = segments[index];
    if (IsLongRest(segment.kind)) {
      if (!current.members.empty()) {
        blocks.push_back(current);
        current = Block{};
      }
      continue;
    }
    if (current.members.empty()) {
      current.beginMinutes = segment.start.TotalMinutes();
    }
    current.members.push_back(index);
    current.endMinutes = segment.end.TotalMinutes();
  }
  if (!current.members.empty()) blocks.push_back(current);
  return blocks;
}

size_t PickBlock(const std::vector<Block>& blocks, int nowMinutes) {
  // 当前时刻落入哪一块（按块首末时间判断，允许块内空档）。
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (nowMinutes < blocks[i].endMinutes) return i;
  }
  return blocks.size() - 1;
}

bool IsWithin(const Segment& segment, int minutes) {
  return minutes >= segment.start.TotalMinutes() && minutes < segment.end.TotalMinutes();
}

}  // namespace

const char* DayStateToString(DayState state) noexcept {
  switch (state) {
    case DayState::Idle: return "idle";
    case DayState::Focus: return "focus";
    case DayState::Break: return "break";
    case DayState::LongBreak: return "long_break";
    case DayState::Lunch: return "lunch";
  }
  return "idle";
}

const char* BoundaryKindToString(BoundaryKind kind) noexcept {
  switch (kind) {
    case BoundaryKind::FocusStart: return "focus_start";
    case BoundaryKind::FocusEnd: return "focus_end";
    case BoundaryKind::BreakStart: return "break_start";
    case BoundaryKind::BreakEnd: return "break_end";
    case BoundaryKind::LongBreakStart: return "long_break_start";
    case BoundaryKind::LongBreakEnd: return "long_break_end";
    case BoundaryKind::LunchStart: return "lunch_start";
    case BoundaryKind::LunchEnd: return "lunch_end";
  }
  return "focus_start";
}

BoundaryKind StartBoundaryKind(SegmentKind kind) noexcept {
  switch (kind) {
    case SegmentKind::Focus: return BoundaryKind::FocusStart;
    case SegmentKind::Break: return BoundaryKind::BreakStart;
    case SegmentKind::LongBreak: return BoundaryKind::LongBreakStart;
    case SegmentKind::Lunch: return BoundaryKind::LunchStart;
  }
  return BoundaryKind::FocusStart;
}

BoundaryKind EndBoundaryKind(SegmentKind kind) noexcept {
  switch (kind) {
    case SegmentKind::Focus: return BoundaryKind::FocusEnd;
    case SegmentKind::Break: return BoundaryKind::BreakEnd;
    case SegmentKind::LongBreak: return BoundaryKind::LongBreakEnd;
    case SegmentKind::Lunch: return BoundaryKind::LunchEnd;
  }
  return BoundaryKind::FocusEnd;
}

Snapshot Evaluate(const std::vector<Segment>& segments, const WallClock& now) {
  Snapshot snapshot;
  snapshot.hasSchedule = !segments.empty();
  if (segments.empty()) return snapshot;

  const auto sorted = SortedIndices(segments);
  const int nowMinutes = now.MinutesSinceMidnight();
  const int nowSeconds = SecondsOfDay(now);

  int focusSeen = 0;
  for (const size_t index : sorted) {
    const Segment& segment = segments[index];
    if (segment.kind == SegmentKind::Focus) ++focusSeen;
    if (!IsWithin(segment, nowMinutes)) continue;

    const int ordinal = segment.kind == SegmentKind::Focus ? focusSeen : 0;
    snapshot.current = MakeRef(segment, ordinal);
    snapshot.state = StateOf(segment.kind);

    const int startSeconds = segment.start.TotalMinutes() * 60;
    const int durationSeconds = segment.DurationMinutes() * 60;
    if (durationSeconds > 0) {
      const double raw = static_cast<double>(nowSeconds - startSeconds) /
                         static_cast<double>(durationSeconds);
      snapshot.progress = std::max(0.0, std::min(1.0, raw));
    }
    break;
  }

  // 下一个时段：开始时间严格晚于当前时刻。
  for (const size_t index : sorted) {
    if (segments[index].start.TotalMinutes() > nowMinutes) {
      int ordinal = 0;
      if (segments[index].kind == SegmentKind::Focus) {
        int count = 0;
        for (const size_t scan : sorted) {
          if (segments[scan].kind == SegmentKind::Focus) ++count;
          if (scan == index) break;
        }
        ordinal = count;
      }
      snapshot.next = MakeRef(segments[index], ordinal);
      break;
    }
  }

  // 下一边界：当前段结束优先；否则为下一个时段的开始。
  if (snapshot.current.has_value()) {
    const SegmentRef& current = *snapshot.current;
    WallClock boundary = now;
    boundary.hour = current.end.hour;
    boundary.minute = current.end.minute;
    boundary.second = 0;
    snapshot.nextBoundary = boundary;
    snapshot.nextBoundaryKind = EndBoundaryKind(current.kind);
  } else if (snapshot.next.has_value()) {
    WallClock boundary = now;
    boundary.hour = snapshot.next->start.hour;
    boundary.minute = snapshot.next->start.minute;
    boundary.second = 0;
    snapshot.nextBoundary = boundary;
    snapshot.nextBoundaryKind = StartBoundaryKind(snapshot.next->kind);
  }

  return snapshot;
}

std::vector<BoundaryEvent> EventsAt(const std::vector<Segment>& segments, const WallClock& now) {
  std::vector<BoundaryEvent> events;
  const auto sorted = SortedIndices(segments);
  const int nowSeconds = SecondsOfDay(now);

  int focusSeen = 0;
  for (const size_t index : sorted) {
    const Segment& segment = segments[index];
    if (segment.kind == SegmentKind::Focus) ++focusSeen;
    const int ordinal = segment.kind == SegmentKind::Focus ? focusSeen : 0;

    if (segment.start.TotalMinutes() * 60 == nowSeconds) {
      events.push_back({StartBoundaryKind(segment.kind), MakeRef(segment, ordinal), now});
    }
    if (segment.end.TotalMinutes() * 60 == nowSeconds) {
      events.push_back({EndBoundaryKind(segment.kind), MakeRef(segment, ordinal), now});
    }
  }
  return events;
}

std::string EventKey(const std::string& dateKey, const std::string& segmentId, BoundaryKind kind,
                     int planRevision) {
  return dateKey + "|" + segmentId + "|" + BoundaryKindToString(kind) + "|" +
         std::to_string(planRevision);
}

std::vector<size_t> VisibleSegmentIndices(const std::vector<Segment>& segments, const WallClock& now,
                                          Scope scope) {
  const auto sorted = SortedIndices(segments);
  if (scope == Scope::AllDay) return sorted;

  const auto blocks = SplitBlocks(segments, sorted);
  if (blocks.empty()) return {};

  const int nowMinutes = now.MinutesSinceMidnight();
  size_t chosen = 0;

  // 若正处在长休息中，优先展示下一块（没有则上一块）。
  bool inLongRest = false;
  for (const size_t index : sorted) {
    if (IsLongRest(segments[index].kind) && IsWithin(segments[index], nowMinutes)) {
      inLongRest = true;
      break;
    }
  }

  if (inLongRest) {
    size_t nextBlock = blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i) {
      if (blocks[i].beginMinutes >= nowMinutes) {
        nextBlock = i;
        break;
      }
    }
    if (nextBlock != blocks.size()) {
      chosen = nextBlock;
    } else {
      size_t previous = 0;
      for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].endMinutes <= nowMinutes) previous = i;
      }
      chosen = previous;
    }
  } else {
    chosen = PickBlock(blocks, nowMinutes);
  }

  return blocks[chosen].members;
}

std::optional<BlockBounds> CurrentBlockBounds(const std::vector<Segment>& segments,
                                              const WallClock& now) {
  const auto sorted = SortedIndices(segments);
  const auto blocks = SplitBlocks(segments, sorted);
  if (blocks.empty()) return std::nullopt;
  const size_t chosen = PickBlock(blocks, now.MinutesSinceMidnight());
  const Block& block = blocks[chosen];
  BlockBounds bounds;
  bounds.begin = *TimeOfDay::FromTotalMinutes(block.beginMinutes);
  bounds.end = *TimeOfDay::FromTotalMinutes(block.endMinutes);
  bounds.firstSortedIndex = 0;
  bounds.lastSortedIndex = 0;
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (sorted[i] == block.members.front()) bounds.firstSortedIndex = i;
    if (sorted[i] == block.members.back()) bounds.lastSortedIndex = i;
  }
  return bounds;
}

int FocusCount(const std::vector<Segment>& segments) {
  int count = 0;
  for (const auto& segment : segments) {
    if (segment.kind == SegmentKind::Focus) ++count;
  }
  return count;
}

}  // namespace classtime
