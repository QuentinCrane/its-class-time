// 作息数据模型：与 shared/schemas/schedule.schema.json 一一对应。
import Foundation

public enum SegmentKind: String, Codable, CaseIterable, Sendable {
  case focus = "focus"
  case shortBreak = "break"
  case longBreak = "long_break"
  case lunch = "lunch"

  // 午休与长休息是“分块分隔符”，短课间不是。
  public var isLongRest: Bool {
    self == .lunch || self == .longBreak
  }

  public var isRest: Bool {
    self != .focus
  }
}

public struct TimeOfDay: Equatable, Comparable, Hashable, Sendable {
  public var hour: Int
  public var minute: Int

  public init(hour: Int = 0, minute: Int = 0) {
    self.hour = hour
    self.minute = minute
  }

  public var totalMinutes: Int {
    hour * 60 + minute
  }

  public static func fromTotalMinutes(_ total: Int) -> TimeOfDay? {
    guard total >= 0 && total < 24 * 60 else { return nil }
    return TimeOfDay(hour: total / 60, minute: total % 60)
  }

  public static func parse(_ text: String) -> TimeOfDay? {
    let utf8 = Array(text.utf8)
    guard utf8.count == 5, utf8[2] == UInt8(ascii: ":") else { return nil }
    func digit(_ b: UInt8) -> Int? {
      guard b >= UInt8(ascii: "0") && b <= UInt8(ascii: "9") else { return nil }
      return Int(b - UInt8(ascii: "0"))
    }
    guard let h1 = digit(utf8[0]), let h2 = digit(utf8[1]),
          let m1 = digit(utf8[3]), let m2 = digit(utf8[4]) else {
      return nil
    }
    let h = h1 * 10 + h2
    let m = m1 * 10 + m2
    guard h <= 23 && m <= 59 else { return nil }
    return TimeOfDay(hour: h, minute: m)
  }

  public var descriptionText: String {
    String(format: "%02d:%02d", hour, minute)
  }

  public static func < (lhs: TimeOfDay, rhs: TimeOfDay) -> Bool {
    lhs.totalMinutes < rhs.totalMinutes
  }
}

public struct Segment: Equatable, Identifiable, Sendable {
  public var id: String
  public var label: String
  public var kind: SegmentKind
  public var start: TimeOfDay
  public var end: TimeOfDay

  public init(
    id: String = "",
    label: String = "",
    kind: SegmentKind = .focus,
    start: TimeOfDay = TimeOfDay(),
    end: TimeOfDay = TimeOfDay()
  ) {
    self.id = id
    self.label = label
    self.kind = kind
    self.start = start
    self.end = end
  }

  public var durationMinutes: Int {
    end.totalMinutes - start.totalMinutes
  }
}

public struct Schedule: Equatable, Identifiable, Sendable {
  public var schemaVersion: Int
  public var id: String
  public var name: String
  public var activeWeekdays: [Int]  // 1=周一 … 7=周日
  public var segments: [Segment]

  public init(
    schemaVersion: Int = 1,
    id: String = "",
    name: String = "",
    activeWeekdays: [Int] = [],
    segments: [Segment] = []
  ) {
    self.schemaVersion = schemaVersion
    self.id = id
    self.name = name
    self.activeWeekdays = activeWeekdays
    self.segments = segments
  }

  public func isActive(on weekday: Int) -> Bool {
    activeWeekdays.contains(weekday)
  }
}

// 本地墙上时间。引擎不依赖系统时区数据库，跨日/夏令时由调用方重新定位。
public struct WallClock: Equatable, Sendable {
  public var year: Int
  public var month: Int
  public var day: Int
  public var hour: Int
  public var minute: Int
  public var second: Int

  public init(
    year: Int = 1970,
    month: Int = 1,
    day: Int = 1,
    hour: Int = 0,
    minute: Int = 0,
    second: Int = 0
  ) {
    self.year = year
    self.month = month
    self.day = day
    self.hour = hour
    self.minute = minute
    self.second = second
  }

  // Howard Hinnant days_from_civil：1=周一 … 7=周日
  public var weekday: Int {
    let y = year - (month <= 2 ? 1 : 0)
    let era = (y >= 0 ? y : y - 399) / 400
    let yoe = y - era * 400
    let m = month
    let doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + day - 1
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy
    let days = era * 146097 + doe - 719468
    return (((days % 7) + 7 + 3) % 7) + 1
  }

  public var minutesSinceMidnight: Int {
    hour * 60 + minute
  }

  public var secondsSinceMidnight: Int {
    hour * 3600 + minute * 60 + second
  }

  public var dateKey: String {
    String(format: "%04d-%02d-%02d", year, month, day)
  }

  public var isoString: String {
    String(format: "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, minute, second)
  }

  public static func parseIso(_ text: String) -> WallClock? {
    let utf8 = Array(text.utf8)
    guard utf8.count == 19,
          utf8[4] == UInt8(ascii: "-"),
          utf8[7] == UInt8(ascii: "-"),
          utf8[10] == UInt8(ascii: "T"),
          utf8[13] == UInt8(ascii: ":"),
          utf8[16] == UInt8(ascii: ":") else {
      return nil
    }
    func number(offset: Int, length: Int) -> Int? {
      var value = 0
      for i in 0..<length {
        let c = utf8[offset + i]
        guard c >= UInt8(ascii: "0") && c <= UInt8(ascii: "9") else { return nil }
        value = value * 10 + Int(c - UInt8(ascii: "0"))
      }
      return value
    }
    guard let y = number(offset: 0, length: 4),
          let m = number(offset: 5, length: 2),
          let d = number(offset: 8, length: 2),
          let hh = number(offset: 11, length: 2),
          let mm = number(offset: 14, length: 2),
          let ss = number(offset: 17, length: 2),
          isValidDate(year: y, month: m, day: d),
          hh <= 23 && mm <= 59 && ss <= 59 else {
      return nil
    }
    return WallClock(year: y, month: m, day: d, hour: hh, minute: mm, second: ss)
  }

  private static func isValidDate(year: Int, month: Int, day: Int) -> Bool {
    guard (1900...9999).contains(year), (1...12).contains(month) else { return false }
    let daysInMonth = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    var limit = daysInMonth[month - 1]
    let leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)
    if month == 2 && leap { limit = 29 }
    return day >= 1 && day <= limit
  }
}

public struct ValidationIssue: Equatable, Sendable {
  public var segmentId: String
  public var message: String

  public init(segmentId: String, message: String) {
    self.segmentId = segmentId
    self.message = message
  }
}

// 按开始时间稳定排序；同一开始时间保持输入顺序。
public func sortedByStart(_ segments: [Segment]) -> [Segment] {
  segments.enumerated()
    .sorted { lhs, rhs in
      if lhs.element.start.totalMinutes != rhs.element.start.totalMinutes {
        return lhs.element.start.totalMinutes < rhs.element.start.totalMinutes
      }
      return lhs.offset < rhs.offset
    }
    .map(\.element)
}

public func sortedIndicesByStart(_ segments: [Segment]) -> [Int] {
  segments.indices.sorted { lhs, rhs in
    if segments[lhs].start.totalMinutes != segments[rhs].start.totalMinutes {
      return segments[lhs].start.totalMinutes < segments[rhs].start.totalMinutes
    }
    return lhs < rhs
  }
}

public func findSegmentById(_ segments: [Segment], _ id: String) -> Segment? {
  segments.first { $0.id == id }
}

// 校验：id 唯一、名称非空、00:00–23:59、结束晚于开始、不存在重叠（相邻允许）。
@discardableResult
public func validateSegments(_ segments: [Segment], issues: inout [ValidationIssue]) -> Bool {
  var local: [ValidationIssue] = []
  var ids = Set<String>()

  for segment in segments {
    if segment.id.isEmpty {
      local.append(ValidationIssue(segmentId: segment.id, message: "时段缺少稳定 ID"))
    } else if !ids.insert(segment.id).inserted {
      local.append(ValidationIssue(segmentId: segment.id, message: "时段 ID 重复"))
    }
    if segment.label.isEmpty {
      local.append(ValidationIssue(segmentId: segment.id, message: "名称不能为空"))
    }
    if segment.start.totalMinutes >= segment.end.totalMinutes {
      local.append(ValidationIssue(segmentId: segment.id, message: "结束时间必须晚于开始时间"))
    }
  }

  let sorted = sortedByStart(segments)
  if sorted.count > 1 {
    for i in 1..<sorted.count {
      if sorted[i].start.totalMinutes < sorted[i - 1].end.totalMinutes {
        local.append(
          ValidationIssue(
            segmentId: sorted[i].id,
            message: "与上一时段重叠（\(sorted[i - 1].label)）"
          )
        )
      }
    }
  }

  issues = local
  return local.isEmpty
}

public func validateSegments(_ segments: [Segment]) -> Bool {
  var ignored: [ValidationIssue] = []
  return validateSegments(segments, issues: &ignored)
}

@discardableResult
public func validateSchedule(_ schedule: Schedule, issues: inout [ValidationIssue]) -> Bool {
  var local: [ValidationIssue] = []
  if schedule.id.isEmpty {
    local.append(ValidationIssue(segmentId: "", message: "模板 ID 不能为空"))
  }
  if schedule.name.isEmpty {
    local.append(ValidationIssue(segmentId: "", message: "模板名称不能为空"))
  }
  for weekday in schedule.activeWeekdays where weekday < 1 || weekday > 7 {
    local.append(ValidationIssue(segmentId: "", message: "星期取值必须在 1–7"))
  }
  var segmentIssues: [ValidationIssue] = []
  _ = validateSegments(schedule.segments, issues: &segmentIssues)
  local.append(contentsOf: segmentIssues)
  issues = local
  return local.isEmpty
}

public func validateSchedule(_ schedule: Schedule) -> Bool {
  var ignored: [ValidationIssue] = []
  return validateSchedule(schedule, issues: &ignored)
}
