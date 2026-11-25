// 同一份当前作息驱动悬浮组件和设置中的预览。
#pragma once

#include <algorithm>
#include <vector>

#include "classtime/config.h"
#include "classtime/time_engine.h"

namespace ClassTimeApp {

struct WidgetState {
  classtime::Snapshot snapshot;
  std::vector<classtime::Segment> visibleSegments;
  double edgeProgress = 0;
  int focusCompleted = 0;
  int focusTotal = 0;
};

inline WidgetState BuildWidgetState(const std::vector<classtime::Segment>& segments,
                                    const classtime::WallClock& now,
                                    classtime::PresentationSettings::Scope scope) {
  WidgetState state;
  state.snapshot = classtime::Evaluate(segments, now);
  const auto selected = classtime::VisibleSegmentIndices(
      segments, now, scope == classtime::PresentationSettings::Scope::AllDay
                         ? classtime::Scope::AllDay : classtime::Scope::CurrentBlock);
  for (const auto index : selected) {
    if (index < segments.size()) state.visibleSegments.push_back(segments[index]);
  }
  if (segments.empty()) return state;
  int first = segments.front().start.TotalMinutes();
  int last = segments.front().end.TotalMinutes();
  for (const auto& segment : segments) {
    first = std::min(first, segment.start.TotalMinutes());
    last = std::max(last, segment.end.TotalMinutes());
    if (segment.kind == classtime::SegmentKind::Focus) {
      ++state.focusTotal;
      if (segment.end.TotalMinutes() <= now.MinutesSinceMidnight()) ++state.focusCompleted;
    }
  }
  if (scope == classtime::PresentationSettings::Scope::AllDay && last > first) {
    const int elapsed = (now.MinutesSinceMidnight() - first) * 60 + now.second;
    state.edgeProgress = std::clamp(static_cast<double>(elapsed) / ((last - first) * 60), 0.0, 1.0);
  } else {
    state.edgeProgress = state.snapshot.progress;
  }
  return state;
}

inline double WidgetSegmentProgress(const classtime::Segment& segment,
                                    const classtime::WallClock& now,
                                    const classtime::Snapshot& snapshot) {
  if (snapshot.current && snapshot.current->id == segment.id) return snapshot.progress;
  return segment.end.TotalMinutes() * 60 <= now.MinutesSinceMidnight() * 60 + now.second ? 1.0 : 0.0;
}

}  // namespace ClassTimeApp
