// 时间轴拖动只改变用户拖动的量，保留已有的分钟精度。
#pragma once

#include <algorithm>
#include <cmath>

namespace ClassTimeApp {

enum class TimelineDragMode { None, Move, ResizeStart, ResizeEnd };

struct TimelineRange {
  int start;
  int end;
};

inline TimelineRange DraggedTimelineRange(TimelineRange original, TimelineDragMode mode,
                                          int deltaMinutes, int stepMinutes) {
  const int step = std::max(1, stepMinutes);
  const int delta = static_cast<int>(std::lround(deltaMinutes / static_cast<double>(step))) * step;
  switch (mode) {
    case TimelineDragMode::Move: {
      const int duration = original.end - original.start;
      const int start = std::clamp(original.start + delta, 0, 1439 - duration);
      return {start, start + duration};
    }
    case TimelineDragMode::ResizeStart:
      return {std::clamp(original.start + delta, 0, original.end - 1), original.end};
    case TimelineDragMode::ResizeEnd:
      return {original.start, std::clamp(original.end + delta, original.start + 1, 1439)};
    default:
      return original;
  }
}

}  // namespace ClassTimeApp
