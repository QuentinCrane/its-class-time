// 时间引擎：只做“给定作息与当下墙上时间 → 状态/进度/下一边界”的确定性计算。
import Foundation

public enum DayState: String, Equatable, Sendable {
  case idle = "idle"
  case focus = "focus"
  case shortBreak = "break"
  case longBreak = "long_break"
  case lunch = "lunch"
}

public enum BoundaryKind: String, Equatable, Sendable {
  case focusStart = "focus_start"
  case focusEnd = "focus_end"
  case breakStart = "break_start"
  case breakEnd = "break_end"
  case longBreakStart = "long_break_start"
  case longBreakEnd = "long_break_end"
  case lunchStart = "lunch_start"
  case lunchEnd = "lunch_end"
}

public func startBoundaryKind(_ kind: SegmentKind) -> BoundaryKind {
  switch kind {
  case .focus: return .focusStart
  case .shortBreak: return .breakStart
  case .longBreak: return .longBreakStart
  case .lunch: return .lunchStart
  }
}

public func endBoundaryKind(_ kind: SegmentKind) -> BoundaryKind {
  switch kind {
  case .focus: return .focusEnd
  case .shortBreak: return .breakEnd
  case .longBreak: return .longBreakEnd
  case .lunch: return .lunchEnd
  }
}

public struct SegmentRef: Equatable, Sendable {
  public var id: String
  public var label: String
  public var kind: SegmentKind
  public var start: TimeOfDay
  public var end: TimeOfDay
  public var focusOrdinal: Int  // 仅 focus 有效，1 起；休息为 0。

  public init(
    id: String,
    label: String,
    kind: SegmentKind,
    start: TimeOfDay,
    end: TimeOfDay,
    focusOrdinal: Int
  ) {
    self.id = id
    self.label = label
    self.kind = kind
    self.start = start
    self.end = end
    self.focusOrdinal = focusOrdinal
  }

  public var durationMinutes: Int {
    end.totalMinutes - start.totalMinutes
  }
}

public struct Snapshot: Equatable, Sendable {
  public var hasSchedule: Bool
  public var state: DayState
  public var current: SegmentRef?
  public var next: SegmentRef?
  public var progress: Double
  public var nextBoundary: WallClock?
  public var nextBoundaryKind: BoundaryKind?

  public init(
    hasSchedule: Bool = false,
    state: DayState = .idle,
    current: SegmentRef? = nil,
    next: SegmentRef? = nil,
    progress: Double = 0.0,
    nextBoundary: WallClock? = nil,
    nextBoundaryKind: BoundaryKind? = nil
  ) {
    self.hasSchedule = hasSchedule
    self.state = state
    self.current = current
    self.next = next
    self.progress = progress
    self.nextBoundary = nextBoundary
    self.nextBoundaryKind = nextBoundaryKind
  }
}

public struct BoundaryEvent: Equatable, Sendable {
  public var kind: BoundaryKind
  public var segment: SegmentRef
  public var at: WallClock

  public init(kind: BoundaryKind, segment: SegmentRef, at: WallClock) {
    self.kind = kind
    self.segment = segment
    self.at = at
  }
}

public enum Scope: Equatable, Sendable {
  case allDay
  case currentBlock
}

public struct BlockBounds: Equatable, Sendable {
  public var begin: TimeOfDay
  public var end: TimeOfDay
  public var firstSortedIndex: Int
  public var lastSortedIndex: Int
}

private func makeRef(_ segment: Segment, focusOrdinal: Int) -> SegmentRef {
  SegmentRef(
    id: segment.id,
    label: segment.label,
    kind: segment.kind,
    start: segment.start,
    end: segment.end,
    focusOrdinal: focusOrdinal
  )
}

private func stateOf(_ kind: SegmentKind) -> DayState {
  switch kind {
  case .focus: return .focus
  case .shortBreak: return .shortBreak
  case .longBreak: return .longBreak
  case .lunch: return .lunch
  }
}

private func isWithin(_ segment: Segment, minutes: Int) -> Bool {
  minutes >= segment.start.totalMinutes && minutes < segment.end.totalMinutes
}

public func evaluate(_ segments: [Segment], now: WallClock) -> Snapshot {
  var snapshot = Snapshot(hasSchedule: !segments.isEmpty)
  guard !segments.isEmpty else { return snapshot }

  let sorted = sortedIndicesByStart(segments)
  let nowMinutes = now.minutesSinceMidnight
  let nowSeconds = now.secondsSinceMidnight

  var focusSeen = 0
  for index in sorted {
    let segment = segments[index]
    if segment.kind == .focus { focusSeen += 1 }
    guard isWithin(segment, minutes: nowMinutes) else { continue }

    let ordinal = segment.kind == .focus ? focusSeen : 0
    snapshot.current = makeRef(segment, focusOrdinal: ordinal)
    snapshot.state = stateOf(segment.kind)

    let startSeconds = segment.start.totalMinutes * 60
    let durationSeconds = segment.durationMinutes * 60
    if durationSeconds > 0 {
      let raw = Double(nowSeconds - startSeconds) / Double(durationSeconds)
      snapshot.progress = max(0.0, min(1.0, raw))
    }
    break
  }

  for index in sorted {
    if segments[index].start.totalMinutes > nowMinutes {
      var ordinal = 0
      if segments[index].kind == .focus {
        var count = 0
        for scan in sorted {
          if segments[scan].kind == .focus { count += 1 }
          if scan == index { break }
        }
        ordinal = count
      }
      snapshot.next = makeRef(segments[index], focusOrdinal: ordinal)
      break
    }
  }

  if let current = snapshot.current {
    var boundary = now
    boundary.hour = current.end.hour
    boundary.minute = current.end.minute
    boundary.second = 0
    snapshot.nextBoundary = boundary
    snapshot.nextBoundaryKind = endBoundaryKind(current.kind)
  } else if let next = snapshot.next {
    var boundary = now
    boundary.hour = next.start.hour
    boundary.minute = next.start.minute
    boundary.second = 0
    snapshot.nextBoundary = boundary
    snapshot.nextBoundaryKind = startBoundaryKind(next.kind)
  }

  return snapshot
}

// 返回恰好发生在 now（精确到秒）的边界事件，最多两个（一段结束 + 下一段开始）。
public func eventsAt(_ segments: [Segment], now: WallClock) -> [BoundaryEvent] {
  var events: [BoundaryEvent] = []
  let sorted = sortedIndicesByStart(segments)
  let nowSeconds = now.secondsSinceMidnight

  var focusSeen = 0
  for index in sorted {
    let segment = segments[index]
    if segment.kind == .focus { focusSeen += 1 }
    let ordinal = segment.kind == .focus ? focusSeen : 0
    let ref = makeRef(segment, focusOrdinal: ordinal)

    if segment.start.totalMinutes * 60 == nowSeconds {
      events.append(BoundaryEvent(kind: startBoundaryKind(segment.kind), segment: ref, at: now))
    }
    if segment.end.totalMinutes * 60 == nowSeconds {
      events.append(BoundaryEvent(kind: endBoundaryKind(segment.kind), segment: ref, at: now))
    }
  }
  return events
}

// 去重键：日期 + 段 ID + 边界类型 + 计划修订号。
public func eventKey(
  dateKey: String,
  segmentId: String,
  kind: BoundaryKind,
  planRevision: Int
) -> String {
  "\(dateKey)|\(segmentId)|\(kind.rawValue)|\(planRevision)"
}

private struct Block {
  var members: [Int] = []
  var beginMinutes: Int = 0
  var endMinutes: Int = 0
}

private func splitBlocks(_ segments: [Segment], sorted: [Int]) -> [Block] {
  var blocks: [Block] = []
  var current = Block()
  for index in sorted {
    let segment = segments[index]
    if segment.kind.isLongRest {
      if !current.members.isEmpty {
        blocks.append(current)
        current = Block()
      }
      continue
    }
    if current.members.isEmpty {
      current.beginMinutes = segment.start.totalMinutes
    }
    current.members.append(index)
    current.endMinutes = segment.end.totalMinutes
  }
  if !current.members.isEmpty {
    blocks.append(current)
  }
  return blocks
}

private func pickBlock(_ blocks: [Block], nowMinutes: Int) -> Int {
  for i in blocks.indices {
    if nowMinutes < blocks[i].endMinutes { return i }
  }
  return blocks.count - 1
}

public func visibleSegmentIndices(
  _ segments: [Segment],
  now: WallClock,
  scope: Scope
) -> [Int] {
  let sorted = sortedIndicesByStart(segments)
  if scope == .allDay { return sorted }

  let nowMinutes = now.minutesSinceMidnight
  let currentLongRest = sorted.first {
    segments[$0].kind.isLongRest && isWithin(segments[$0], minutes: nowMinutes)
  }

  let blocks = splitBlocks(segments, sorted: sorted)
  // 一天只有长休息时，当前休息仍须可见。
  if blocks.isEmpty {
    return currentLongRest.map { [$0] } ?? []
  }

  let chosen: Int
  // 长休息本身与下一块一起展示；没有下一块则保留上一块。
  if currentLongRest != nil {
    if let nextBlock = blocks.firstIndex(where: { $0.beginMinutes >= nowMinutes }) {
      chosen = nextBlock
    } else {
      var previous = 0
      for i in blocks.indices where blocks[i].endMinutes <= nowMinutes {
        previous = i
      }
      chosen = previous
    }
  } else {
    chosen = pickBlock(blocks, nowMinutes: nowMinutes)
  }

  var visible = blocks[chosen].members
  if let restIndex = currentLongRest {
    visible.append(restIndex)
    visible = visible.enumerated()
      .sorted { lhs, rhs in
        let lStart = segments[lhs.element].start.totalMinutes
        let rStart = segments[rhs.element].start.totalMinutes
        if lStart != rStart { return lStart < rStart }
        return lhs.offset < rhs.offset
      }
      .map(\.element)
  }
  return visible
}

public func currentBlockBounds(_ segments: [Segment], now: WallClock) -> BlockBounds? {
  let sorted = sortedIndicesByStart(segments)
  let blocks = splitBlocks(segments, sorted: sorted)
  guard !blocks.isEmpty else { return nil }
  let chosen = pickBlock(blocks, nowMinutes: now.minutesSinceMidnight)
  let block = blocks[chosen]
  guard let begin = TimeOfDay.fromTotalMinutes(block.beginMinutes),
        let end = TimeOfDay.fromTotalMinutes(block.endMinutes) else {
    return nil
  }
  var firstSorted = 0
  var lastSorted = 0
  for i in sorted.indices {
    if sorted[i] == block.members.first { firstSorted = i }
    if sorted[i] == block.members.last { lastSorted = i }
  }
  return BlockBounds(
    begin: begin,
    end: end,
    firstSortedIndex: firstSorted,
    lastSortedIndex: lastSorted
  )
}

public func focusCount(_ segments: [Segment]) -> Int {
  segments.reduce(0) { $0 + ($1.kind == .focus ? 1 : 0) }
}
