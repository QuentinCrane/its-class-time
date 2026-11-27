// 同一份当前作息驱动悬浮组件与设置中的实时预览（与 Windows widget_state.h 规则严格对等）。
import Foundation

public struct WidgetState: Equatable, Sendable {
  public var snapshot: Snapshot
  public var visibleSegments: [Segment]
  public var edgeProgress: Double
  public var focusCompleted: Int
  public var focusTotal: Int

  public init(
    snapshot: Snapshot = Snapshot(),
    visibleSegments: [Segment] = [],
    edgeProgress: Double = 0,
    focusCompleted: Int = 0,
    focusTotal: Int = 0
  ) {
    self.snapshot = snapshot
    self.visibleSegments = visibleSegments
    self.edgeProgress = edgeProgress
    self.focusCompleted = focusCompleted
    self.focusTotal = focusTotal
  }
}

// 全天边框从首段开始到末段结束；空档占据真实时间，未到与已过分别呈现。
public struct ScreenEdgeRun: Equatable, Sendable {
  public var begin: Double
  public var end: Double
  public var kind: SegmentKind?  // nil 表示空档
  public var elapsed: Bool

  public init(begin: Double, end: Double, kind: SegmentKind?, elapsed: Bool) {
    self.begin = begin
    self.end = end
    self.kind = kind
    self.elapsed = elapsed
  }
}

public func buildScreenEdgeRuns(
  segments: [Segment],
  now: WallClock
) -> [ScreenEdgeRun] {
  let sorted = sortedByStart(segments)
  guard let firstSegment = sorted.first else { return [] }
  let first = firstSegment.start.totalMinutes
  var last = first
  for segment in sorted {
    last = max(last, segment.end.totalMinutes)
  }
  guard last > first else { return [] }

  let duration = Double(last - first)
  let rawProgress =
    (Double(now.minutesSinceMidnight) * 60.0 + Double(now.second) - Double(first) * 60.0)
    / (duration * 60.0)
  let progress = min(1.0, max(0.0, rawProgress))

  var runs: [ScreenEdgeRun] = []
  func appendRun(from: Int, to: Int, kind: SegmentKind?) {
    let begin = Double(from - first) / duration
    let end = Double(to - first) / duration
    guard end > begin else { return }
    if begin < progress {
      runs.append(ScreenEdgeRun(begin: begin, end: min(end, progress), kind: kind, elapsed: true))
    }
    if end > progress {
      runs.append(ScreenEdgeRun(begin: max(begin, progress), end: end, kind: kind, elapsed: false))
    }
  }

  var cursor = first
  for segment in sorted {
    let start = segment.start.totalMinutes
    let end = segment.end.totalMinutes
    if start > cursor {
      appendRun(from: cursor, to: start, kind: nil)
    }
    if end > max(cursor, start) {
      appendRun(from: max(cursor, start), to: end, kind: segment.kind)
    }
    cursor = max(cursor, end)
  }
  return runs
}

public struct ScreenEdgeBlendStop: Equatable, Sendable {
  public var position: Double
  public var kind: SegmentKind?
  public var elapsed: Bool

  public init(position: Double, kind: SegmentKind?, elapsed: Bool) {
    self.position = position
    self.kind = kind
    self.elapsed = elapsed
  }
}

// 只在交界附近过渡；短时段自动缩短过渡，避免把它完全混进两侧。
public func buildScreenEdgeBlendStops(_ runs: [ScreenEdgeRun]) -> [ScreenEdgeBlendStop] {
  guard let first = runs.first, let last = runs.last else { return [] }
  var stops: [ScreenEdgeBlendStop] = [
    ScreenEdgeBlendStop(position: first.begin, kind: first.kind, elapsed: first.elapsed)
  ]
  if runs.count > 1 {
    for i in 1..<runs.count {
      let before = runs[i - 1]
      let after = runs[i]
      if before.kind == after.kind && before.elapsed == after.elapsed {
        continue
      }
      let radius = min(
        0.003,
        min((before.end - before.begin) * 0.4, (after.end - after.begin) * 0.4)
      )
      stops.append(
        ScreenEdgeBlendStop(position: before.end - radius, kind: before.kind, elapsed: before.elapsed)
      )
      stops.append(
        ScreenEdgeBlendStop(position: after.begin + radius, kind: after.kind, elapsed: after.elapsed)
      )
    }
  }
  stops.append(ScreenEdgeBlendStop(position: last.end, kind: last.kind, elapsed: last.elapsed))
  return stops
}

// 将时间比例沿顶部、右侧、底部、左侧顺时针切分；每侧的偏移均沿行进方向。
public struct ScreenEdgeSideRun: Equatable, Sendable {
  public var side: Int
  public var begin: Double
  public var end: Double
  public var kind: SegmentKind?
  public var elapsed: Bool

  public init(side: Int, begin: Double, end: Double, kind: SegmentKind?, elapsed: Bool) {
    self.side = side
    self.begin = begin
    self.end = end
    self.kind = kind
    self.elapsed = elapsed
  }
}

public func projectScreenEdgeRuns(
  _ runs: [ScreenEdgeRun],
  lengths: [Double]
) -> [ScreenEdgeSideRun] {
  guard lengths.count == 4 else { return [] }
  let perimeter = lengths.reduce(0.0, +)
  guard perimeter > 0 else { return [] }

  var result: [ScreenEdgeSideRun] = []
  var sideStart = 0.0
  for side in 0..<4 {
    let sideEnd = sideStart + lengths[side]
    for run in runs {
      let begin = max(sideStart, run.begin * perimeter)
      let end = min(sideEnd, run.end * perimeter)
      if end > begin {
        result.append(
          ScreenEdgeSideRun(
            side: side,
            begin: begin - sideStart,
            end: end - sideStart,
            kind: run.kind,
            elapsed: run.elapsed
          )
        )
      }
    }
    sideStart = sideEnd
  }
  return result
}

public func buildWidgetState(
  segments: [Segment],
  now: WallClock,
  scope: PresentationSettings.ScopeMode
) -> WidgetState {
  var state = WidgetState()
  state.snapshot = evaluate(segments, now: now)
  let engineScope: Scope = (scope == .allDay) ? .allDay : .currentBlock
  let selected = visibleSegmentIndices(segments, now: now, scope: engineScope)
  state.visibleSegments = selected.compactMap { $0 < segments.count ? segments[$0] : nil }

  guard let firstSegment = segments.first else { return state }
  var first = firstSegment.start.totalMinutes
  var last = firstSegment.end.totalMinutes
  for segment in segments {
    first = min(first, segment.start.totalMinutes)
    last = max(last, segment.end.totalMinutes)
    if segment.kind == .focus {
      state.focusTotal += 1
      if segment.end.totalMinutes <= now.minutesSinceMidnight {
        state.focusCompleted += 1
      }
    }
  }

  if scope == .allDay && last > first {
    let elapsed = (now.minutesSinceMidnight - first) * 60 + now.second
    state.edgeProgress = min(1.0, max(0.0, Double(elapsed) / Double((last - first) * 60)))
  } else {
    state.edgeProgress = state.snapshot.progress
  }
  return state
}

public func widgetSegmentProgress(
  segment: Segment,
  now: WallClock,
  snapshot: Snapshot
) -> Double {
  if let current = snapshot.current, current.id == segment.id {
    return snapshot.progress
  }
  return segment.end.totalMinutes * 60 <= now.secondsSinceMidnight ? 1.0 : 0.0
}
