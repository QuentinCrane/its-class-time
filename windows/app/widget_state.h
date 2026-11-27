// 同一份当前作息驱动悬浮组件和设置中的预览。
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>
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

// 全天边框从首段开始到末段结束；空档占据真实时间，未到与已过分别呈现。
struct ScreenEdgeRun {
  double begin = 0;
  double end = 0;
  std::optional<classtime::SegmentKind> kind;  // 空值为空档
  bool elapsed = false;
};

inline std::vector<ScreenEdgeRun> BuildScreenEdgeRuns(
    const std::vector<classtime::Segment>& segments, const classtime::WallClock& now) {
  const auto sorted = classtime::SortedByStart(segments);
  if (sorted.empty()) return {};
  const int first = sorted.front().start.TotalMinutes();
  int last = first;
  for (const auto& segment : sorted) last = std::max(last, segment.end.TotalMinutes());
  if (last <= first) return {};

  const double duration = static_cast<double>(last - first);
  const double progress = std::clamp(
      (now.MinutesSinceMidnight() * 60.0 + now.second - first * 60.0) /
          (duration * 60.0), 0.0, 1.0);
  std::vector<ScreenEdgeRun> runs;
  const auto append = [&](int from, int to, std::optional<classtime::SegmentKind> kind) {
    const double begin = (from - first) / duration;
    const double end = (to - first) / duration;
    if (end <= begin) return;
    if (begin < progress) runs.push_back({begin, std::min(end, progress), kind, true});
    if (end > progress) runs.push_back({std::max(begin, progress), end, kind, false});
  };
  int cursor = first;
  for (const auto& segment : sorted) {
    const int start = segment.start.TotalMinutes();
    const int end = segment.end.TotalMinutes();
    if (start > cursor) append(cursor, start, std::nullopt);
    if (end > std::max(cursor, start)) append(std::max(cursor, start), end, segment.kind);
    cursor = std::max(cursor, end);
  }
  return runs;
}

struct ScreenEdgeBlendStop {
  double position = 0;
  std::optional<classtime::SegmentKind> kind;
  bool elapsed = false;
};

// 只在交界附近过渡；短时段自动缩短过渡，避免把它完全混进两侧。
inline std::vector<ScreenEdgeBlendStop> BuildScreenEdgeBlendStops(
    const std::vector<ScreenEdgeRun>& runs) {
  if (runs.empty()) return {};
  std::vector<ScreenEdgeBlendStop> stops = {{runs.front().begin, runs.front().kind,
                                             runs.front().elapsed}};
  for (size_t i = 1; i < runs.size(); ++i) {
    const auto& before = runs[i - 1];
    const auto& after = runs[i];
    if (before.kind == after.kind && before.elapsed == after.elapsed) continue;
    const double radius = std::min({0.003, (before.end - before.begin) * 0.4,
                                    (after.end - after.begin) * 0.4});
    stops.push_back({before.end - radius, before.kind, before.elapsed});
    stops.push_back({after.begin + radius, after.kind, after.elapsed});
  }
  stops.push_back({runs.back().end, runs.back().kind, runs.back().elapsed});
  return stops;
}

// 将时间比例沿顶部、右侧、底部、左侧顺时针切分；每侧的偏移均沿行进方向。
struct ScreenEdgeSideRun {
  size_t side = 0;
  double begin = 0;
  double end = 0;
  std::optional<classtime::SegmentKind> kind;
  bool elapsed = false;
};

inline std::vector<ScreenEdgeSideRun> ProjectScreenEdgeRuns(
    const std::vector<ScreenEdgeRun>& runs, const std::array<double, 4>& lengths) {
  const double perimeter = std::accumulate(lengths.begin(), lengths.end(), 0.0);
  if (perimeter <= 0) return {};
  std::vector<ScreenEdgeSideRun> result;
  double sideStart = 0;
  for (size_t side = 0; side < lengths.size(); ++side) {
    const double sideEnd = sideStart + lengths[side];
    for (const auto& run : runs) {
      const double begin = std::max(sideStart, run.begin * perimeter);
      const double end = std::min(sideEnd, run.end * perimeter);
      if (end > begin) {
        result.push_back({side, begin - sideStart, end - sideStart, run.kind, run.elapsed});
      }
    }
    sideStart = sideEnd;
  }
  return result;
}

// 屏幕边框进度前沿高光：流光扫向当前进度点并在前沿周期性闪烁高光，
// 配合前沿窄对比暗边，避免在浅色背景下产生“断层”错觉。
struct ScreenEdgeHighlightSpec {
  double tailLength = 0;  // 位于 head 后方（已过侧）的流光扫尾长度
  double headSpan = 0;    // 紧贴 head 后方的脉冲高亮核心长度
  double leadLength = 0;  // 位于 head 前方（未到侧）的窄对比暗边长度
};

inline ScreenEdgeHighlightSpec BuildScreenEdgeHighlightSpec(double perimeter, double thickness) {
  if (perimeter <= 0 || thickness <= 0) return {};
  const double lead = std::max(2.0, thickness * 0.32);
  const double headSpan = std::max(6.0, thickness * 2.2);
  const double tail = std::clamp(thickness * 9.0, std::max(24.0, headSpan + 4.0),
                                 std::max(headSpan + 4.0, perimeter * 0.14));
  return {tail, headSpan, lead};
}

struct ScreenEdgeHighlightSample {
  double boost = 0;  // 增强原色饱和度与不透明度，保持边框色带连续不“断层”
  double core = 0;   // 居中白色高光强度（经横截面弧形遮罩后照亮条芯，保留彩色外缘）
  double lip = 0;    // 前沿未到侧的窄暗色对比边，浅色背景下也能清晰定位终点
};

// phase ∈ [0, 1) 为约 1.8s 流光与呼吸闪烁周期；静态模式取峰值相位 0.65。
inline ScreenEdgeHighlightSample SampleScreenEdgeHighlight(double position, double head,
                                                           double perimeter, double thickness,
                                                           double phase = 0.65) {
  if (perimeter <= 0 || head <= perimeter * 0.0001 || head >= perimeter * 0.9999) return {};
  const auto spec = BuildScreenEdgeHighlightSpec(perimeter, thickness);
  if (spec.tailLength <= 0) return {};
  const double delta = head - position;  // >0 在已过侧，<0 在未到侧
  if (delta < -spec.leadLength || delta > spec.tailLength) return {};

  const double wrapped = phase - std::floor(phase);
  // 0.65 处流光汇入前沿并触发前沿脉冲峰值（呼吸闪烁）。
  const double flashDist = std::min(std::fabs(wrapped - 0.65), 1.0 - std::fabs(wrapped - 0.65));
  const double flash = std::exp(-(flashDist * flashDist) / (2.0 * 0.13 * 0.13));
  const double headPulse = 0.28 + 0.72 * flash;

  ScreenEdgeHighlightSample sample;
  if (delta < 0.0) {
    const double u = 1.0 + delta / spec.leadLength;
    sample.lip = u * u * (0.38 + 0.24 * flash);
    return sample;
  }

  // 1) 已过侧靠近前沿的色带能量增强（保持饱和不洗白）
  const double tailFalloff = 1.0 - delta / spec.tailLength;
  sample.boost = tailFalloff * tailFalloff * (0.45 + 0.55 * headPulse);

  // 2) 前沿闪烁高亮核心（随脉冲呼吸闪亮并轻微外扩）
  const double activeHeadSpan = spec.headSpan * (0.75 + 0.45 * flash);
  if (delta <= activeHeadSpan) {
    const double u = delta / activeHeadSpan;
    const double headShape = (1.0 - u) * (1.0 - u);
    sample.core = std::max(sample.core, headShape * headPulse);
  }

  // 3) 顺时针扫向前沿的流光波（phase 0..0.72 从尾端扫入前沿）
  if (wrapped <= 0.72) {
    const double progress = wrapped / 0.72;
    const double waveCenter = spec.tailLength * (1.0 - progress);
    const double waveSigma = std::max(5.0, thickness * 1.35);
    const double waveEnvelope = std::sin(3.141592653589793 * progress);
    const double diff = delta - waveCenter;
    const double wave = waveEnvelope * std::exp(-(diff * diff) / (2.0 * waveSigma * waveSigma));
    sample.core = std::max(sample.core, 0.85 * wave);
    sample.boost = std::max(sample.boost, 0.75 * wave);
  }

  return sample;
}

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
