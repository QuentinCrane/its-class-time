// 时间轴拖动只改变用户拖动的量，保留已有的分钟精度（与 Windows timeline_edit.h 规则严格对等）。
import Foundation

public enum TimelineDragMode: Equatable, Sendable {
  case none
  case move
  case resizeStart
  case resizeEnd
}

public struct TimelineRange: Equatable, Sendable {
  public var start: Int
  public var end: Int

  public init(start: Int, end: Int) {
    self.start = start
    self.end = end
  }
}

public func snapToTimelineStep(_ minute: Int, step: Int) -> Int {
  let safeStep = max(1, step)
  return Int((Double(minute) / Double(safeStep)).rounded()) * safeStep
}

public func draggedTimelineRange(
  original: TimelineRange,
  mode: TimelineDragMode,
  deltaMinutes: Int,
  stepMinutes: Int
) -> TimelineRange {
  let step = max(1, stepMinutes)
  let delta = Int((Double(deltaMinutes) / Double(step)).rounded()) * step
  switch mode {
  case .move:
    let duration = original.end - original.start
    let start = min(max(original.start + delta, 0), 1439 - duration)
    return TimelineRange(start: start, end: start + duration)
  case .resizeStart:
    let start = min(max(original.start + delta, 0), original.end - 1)
    return TimelineRange(start: start, end: original.end)
  case .resizeEnd:
    let end = min(max(original.end + delta, original.start + 1), 1439)
    return TimelineRange(start: original.start, end: end)
  case .none:
    return original
  }
}
