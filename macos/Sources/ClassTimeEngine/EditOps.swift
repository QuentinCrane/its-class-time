// 作息编辑：时长/起止修改、顺延或保持、增删，并在保存前统一校验。
import Foundation

public enum ShiftStrategy: String, Equatable, Sendable {
  case shift = "shift"
  case keep = "keep"
}

public struct EditRequest: Equatable, Sendable {
  public var segmentId: String
  public var start: TimeOfDay?
  public var end: TimeOfDay?
  public var durationMinutes: Int?  // 优先于 end：end = start + duration
  public var label: String?
  public var kind: SegmentKind?
  public var strategy: ShiftStrategy

  public init(
    segmentId: String = "",
    start: TimeOfDay? = nil,
    end: TimeOfDay? = nil,
    durationMinutes: Int? = nil,
    label: String? = nil,
    kind: SegmentKind? = nil,
    strategy: ShiftStrategy = .shift
  ) {
    self.segmentId = segmentId
    self.start = start
    self.end = end
    self.durationMinutes = durationMinutes
    self.label = label
    self.kind = kind
    self.strategy = strategy
  }
}

public enum EditStatus: Equatable, Sendable {
  case ok
  case notFound
  case invalidDuration
  case invalidRange
  case overlap
  case duplicateId
}

public struct EditOutcome: Equatable, Sendable {
  public var status: EditStatus
  public var segments: [Segment]
  public var conflictIds: [String]
  public var message: String

  public init(
    status: EditStatus = .ok,
    segments: [Segment] = [],
    conflictIds: [String] = [],
    message: String = ""
  ) {
    self.status = status
    self.segments = segments
    self.conflictIds = conflictIds
    self.message = message
  }

  public var ok: Bool {
    status == .ok
  }
}

private func collectConflictIds(_ issues: [ValidationIssue]) -> [String] {
  var ids: [String] = []
  for issue in issues where !issue.segmentId.isEmpty {
    if !ids.contains(issue.segmentId) {
      ids.append(issue.segmentId)
    }
  }
  return ids
}

private func validateAndReturn(_ segments: [Segment]) -> EditOutcome {
  var issues: [ValidationIssue] = []
  if !validateSegments(segments, issues: &issues) {
    return EditOutcome(
      status: .overlap,
      segments: segments,
      conflictIds: collectConflictIds(issues),
      message: "修改后时段冲突，请调整时间或选择顺延方案"
    )
  }
  return EditOutcome(status: .ok, segments: sortedByStart(segments))
}

private func shiftSegment(_ segment: inout Segment, deltaMinutes: Int) -> String? {
  let newStart = segment.start.totalMinutes + deltaMinutes
  let newEnd = segment.end.totalMinutes + deltaMinutes
  guard let start = TimeOfDay.fromTotalMinutes(newStart),
        let end = TimeOfDay.fromTotalMinutes(newEnd) else {
    return "顺延后超出当天 00:00–23:59"
  }
  segment.start = start
  segment.end = end
  return nil
}

public func applyEdit(_ base: [Segment], request: EditRequest) -> EditOutcome {
  var sorted = sortedByStart(base)
  guard let position = sorted.firstIndex(where: { $0.id == request.segmentId }) else {
    return EditOutcome(status: .notFound, message: "找不到要修改的时段")
  }

  var edited = sorted[position]
  if let label = request.label { edited.label = label }
  if let kind = request.kind { edited.kind = kind }
  if let start = request.start { edited.start = start }

  if let durationMinutes = request.durationMinutes {
    guard durationMinutes > 0 else {
      return EditOutcome(status: .invalidDuration, message: "时长必须大于 0 分钟")
    }
    let endTotal = edited.start.totalMinutes + durationMinutes
    guard let end = TimeOfDay.fromTotalMinutes(endTotal) else {
      return EditOutcome(status: .invalidRange, message: "时长超出当天范围（结束不得晚于 23:59）")
    }
    edited.end = end
  } else if let end = request.end {
    edited.end = end
  }

  guard edited.end.totalMinutes > edited.start.totalMinutes else {
    return EditOutcome(status: .invalidRange, message: "结束时间必须晚于开始时间")
  }

  let delta = edited.end.totalMinutes - sorted[position].end.totalMinutes
  sorted[position] = edited

  if request.strategy == .shift && delta != 0 && (position + 1) < sorted.count {
    for i in (position + 1)..<sorted.count {
      if let error = shiftSegment(&sorted[i], deltaMinutes: delta) {
        return EditOutcome(status: .invalidRange, conflictIds: [sorted[i].id], message: error)
      }
    }
  }

  return validateAndReturn(sorted)
}

public func insertSegment(_ base: [Segment], segment: Segment) -> EditOutcome {
  if base.contains(where: { $0.id == segment.id }) {
    return EditOutcome(status: .duplicateId, conflictIds: [segment.id], message: "时段 ID 已存在")
  }
  guard segment.end.totalMinutes > segment.start.totalMinutes else {
    return EditOutcome(
      status: .invalidRange,
      conflictIds: [segment.id],
      message: "结束时间必须晚于开始时间"
    )
  }
  var segments = base
  segments.append(segment)
  return validateAndReturn(segments)
}

public func removeSegment(_ base: [Segment], id: String) -> EditOutcome {
  var segments: [Segment] = []
  segments.reserveCapacity(base.count)
  var found = false
  for segment in base {
    if segment.id == id {
      found = true
      continue
    }
    segments.append(segment)
  }
  guard found else {
    return EditOutcome(status: .notFound, message: "找不到要删除的时段")
  }
  return validateAndReturn(segments)
}
