// 用户配置、预设解析、今日覆盖与运行态去重数据。
import Foundation

public struct PresentationSettings: Equatable, Sendable {
  public enum ViewMode: String, CaseIterable, Sendable {
    case bar = "bar"
    case ring = "ring"
    case screenEdge = "screen_edge"
  }

  public enum ScopeMode: String, CaseIterable, Sendable {
    case allDay = "all_day"
    case currentBlock = "current_block"
  }

  public enum Edge: String, CaseIterable, Sendable {
    case top = "top"
    case bottom = "bottom"
    case left = "left"
    case right = "right"

    public var isSide: Bool {
      self == .left || self == .right
    }
  }

  public enum ScreenEdgeColors: String, CaseIterable, Sendable {
    case schedule = "schedule"
    case currentState = "current_state"
  }

  public enum MenuBarDisplayMode: String, CaseIterable, Sendable {
    case iconOnly = "icon_only"
    case countdown = "countdown"
    case detailed = "detailed"
  }

  public enum GlassStyle: String, CaseIterable, Sendable {
    case clear = "clear"
    case regular = "regular"
  }

  public static let minScreenEdgeThickness = 4
  public static let maxScreenEdgeThickness = 24
  public static let defaultScreenEdgeThickness = 10
  public static let minBarLength = 240
  public static let maxBarLength = 1600
  // 0 保留按方向设置的默认长度；非零值为用户指定的 pt。
  public static let defaultBarLength = 0
  public static let defaultHorizontalBarLength = 680
  public static let defaultVerticalBarLength = 420

  public var view: ViewMode = .bar
  public var scope: ScopeMode = .allDay
  public var edge: Edge = .bottom
  public var menuBarDisplay: MenuBarDisplayMode = .countdown
  public var glassStyle: GlassStyle = .clear
  public var visible: Bool = true
  public var autoHideAtEdge: Bool = false
  public var flushWithEdge: Bool = true
  public var screenEdgeThickness: Int = defaultScreenEdgeThickness
  public var screenEdgeColors: ScreenEdgeColors = .schedule
  public var barLengthDip: Int = defaultBarLength
  public var timelineSnapMinutes: Int = 5
  // 长条背景色，"RRGGBB" 十六进制字符串；空表示跟随系统/液态玻璃背景。
  public var barBackground: String = ""
  public var monitorId: String = ""
  public var offsetX: Int = 0
  public var offsetY: Int = 12

  public var effectiveBarLength: Int {
    if barLengthDip != Self.defaultBarLength {
      return barLengthDip
    }
    return edge.isSide ? Self.defaultVerticalBarLength : Self.defaultHorizontalBarLength
  }

  public init() {}
}

public struct NotificationSettings: Equatable, Sendable {
  public var enabled: Bool = true
  public var muted: Bool = false
  public var focusBoundaries: Bool = true
  public var restBoundaries: Bool = true
  public var lunchBoundaries: Bool = true

  public init() {}
}

public struct DailyOverride: Equatable, Sendable {
  public var segments: [Segment]

  public init(segments: [Segment] = []) {
    self.segments = segments
  }
}

public struct AppConfig: Equatable, Sendable {
  public var schemaVersion: Int = 1
  public var revision: Int = 1
  public var selectedTemplateId: String = "high-school"
  public var templates: [Schedule] = []
  public var dailyOverrides: [String: DailyOverride] = [:]
  public var presentation: PresentationSettings = PresentationSettings()
  public var notifications: NotificationSettings = NotificationSettings()
  public var launchAtLogin: Bool = false

  public init() {}
}

public struct RuntimeState: Equatable, Sendable {
  public var firedKeys: [String] = []
  public var lastActiveDate: String = ""

  public init() {}
}

// —— JSON 转换辅助 ——

// "RRGGBB"（大小写均可）；无效或空字符串都视为“跟随系统”。
public func isValidHexColor(_ text: String) -> Bool {
  let bytes = Array(text.utf8)
  guard bytes.count == 6 else { return false }
  for b in bytes {
    let digit = b >= UInt8(ascii: "0") && b <= UInt8(ascii: "9")
    let lower = b >= UInt8(ascii: "a") && b <= UInt8(ascii: "f")
    let upper = b >= UInt8(ascii: "A") && b <= UInt8(ascii: "F")
    if !digit && !lower && !upper { return false }
  }
  return true
}

private func exactVersionOne(_ value: Any?) -> Bool {
  guard let num = value as? NSNumber else { return false }
  if CFGetTypeID(num) == CFBooleanGetTypeID() { return false }
  return num.doubleValue == 1.0
}

private func intFromAny(_ value: Any?) -> Int? {
  if let num = value as? NSNumber {
    if CFGetTypeID(num) == CFBooleanGetTypeID() { return nil }
    return num.intValue
  }
  return value as? Int
}

private func boolFromAny(_ value: Any?, fallback: Bool) -> Bool {
  if let num = value as? NSNumber {
    if CFGetTypeID(num) == CFBooleanGetTypeID() { return num.boolValue }
  }
  if let b = value as? Bool { return b }
  return fallback
}

private func parseSegmentObject(_ raw: Any) -> Result<Segment, ParseError> {
  guard let dict = raw as? [String: Any] else {
    return .failure(ParseError("时段必须是对象"))
  }
  guard let id = dict["id"] as? String,
        let kindText = dict["kind"] as? String,
        let label = dict["label"] as? String,
        let startText = dict["start"] as? String,
        let endText = dict["end"] as? String else {
    return .failure(ParseError("时段缺少 id/kind/label/start/end"))
  }
  guard let kind = SegmentKind(rawValue: kindText) else {
    return .failure(ParseError("未知的时段类型：\(kindText)"))
  }
  guard let start = TimeOfDay.parse(startText),
        let end = TimeOfDay.parse(endText) else {
    return .failure(ParseError("时间必须是 HH:mm（00:00–23:59）"))
  }
  return .success(Segment(id: id, label: label, kind: kind, start: start, end: end))
}

private func parseSegmentsArray(_ raw: Any) -> Result<[Segment], ParseError> {
  guard let list = raw as? [Any] else {
    return .failure(ParseError("segments 必须是数组"))
  }
  var out: [Segment] = []
  out.reserveCapacity(list.count)
  for item in list {
    switch parseSegmentObject(item) {
    case .success(let seg): out.append(seg)
    case .failure(let err): return .failure(err)
    }
  }
  return .success(out)
}

private func parseScheduleObject(_ raw: Any) -> Result<Schedule, ParseError> {
  guard let dict = raw as? [String: Any] else {
    return .failure(ParseError("模板根节点必须是对象"))
  }
  guard let versionAny = dict["schemaVersion"],
        let id = dict["id"] as? String,
        let name = dict["name"] as? String,
        let weekdaysAny = dict["activeWeekdays"],
        let segmentsAny = dict["segments"] else {
    return .failure(ParseError("模板缺少 schemaVersion/id/name/activeWeekdays/segments"))
  }
  guard exactVersionOne(versionAny) else {
    return .failure(ParseError("不支持的 schemaVersion"))
  }
  guard let weekdayList = weekdaysAny as? [Any] else {
    return .failure(ParseError("activeWeekdays 必须是数组"))
  }
  let weekdays = weekdayList.map { intFromAny($0) ?? 0 }

  let segments: [Segment]
  switch parseSegmentsArray(segmentsAny) {
  case .success(let list): segments = list
  case .failure(let err): return .failure(err)
  }

  let schedule = Schedule(
    schemaVersion: 1,
    id: id,
    name: name,
    activeWeekdays: weekdays,
    segments: segments
  )
  guard validateSchedule(schedule) else {
    return .failure(ParseError("模板内容不合法（时间重叠或字段缺失）"))
  }
  return .success(schedule)
}

private func segmentToObject(_ segment: Segment) -> [String: Any] {
  [
    "id": segment.id,
    "kind": segment.kind.rawValue,
    "label": segment.label,
    "start": segment.start.descriptionText,
    "end": segment.end.descriptionText,
  ]
}

private func scheduleToObject(_ schedule: Schedule) -> [String: Any] {
  [
    "schemaVersion": schedule.schemaVersion,
    "id": schedule.id,
    "name": schedule.name,
    "activeWeekdays": schedule.activeWeekdays,
    "segments": schedule.segments.map(segmentToObject),
  ]
}

public struct ParseError: Error, Sendable {
  public let message: String
  public init(_ message: String) { self.message = message }
}

private func decodeJsonObject(_ text: String) -> Result<Any, ParseError> {
  guard let data = text.data(using: .utf8), !data.isEmpty else {
    return .failure(ParseError("JSON 内容为空"))
  }
  do {
    // 不启用 JSON5 扩展，确保尾随逗号等非标准输入被拒绝（与 C++ 引擎一致）。
    let obj = try JSONSerialization.jsonObject(with: data, options: [])
    return .success(obj)
  } catch {
    return .failure(ParseError("JSON 语法错误：\(error.localizedDescription)"))
  }
}

private func dumpJson(_ object: Any, pretty: Bool = true) -> String {
  var options: JSONSerialization.WritingOptions = [.sortedKeys]
  if pretty { options.insert(.prettyPrinted) }
  guard let data = try? JSONSerialization.data(withJSONObject: object, options: options),
        let text = String(data: data, encoding: .utf8) else {
    return "{}"
  }
  return text
}

// —— 预设 ——

public func parsePreset(_ text: String, error: inout String) -> Schedule? {
  error = ""
  switch decodeJsonObject(text) {
  case .failure(let err):
    error = err.message
    return nil
  case .success(let root):
    switch parseScheduleObject(root) {
    case .success(let schedule): return schedule
    case .failure(let err):
      error = err.message
      return nil
    }
  }
}

public func parsePreset(_ text: String) -> Schedule? {
  var ignored = ""
  return parsePreset(text, error: &ignored)
}

public func serializeSchedule(_ schedule: Schedule, pretty: Bool = true) -> String {
  dumpJson(scheduleToObject(schedule), pretty: pretty)
}

// —— 配置 ——

public func parseConfig(_ text: String, error: inout String) -> AppConfig? {
  error = ""
  let rootAny: Any
  switch decodeJsonObject(text) {
  case .failure(let err):
    error = err.message
    return nil
  case .success(let obj):
    rootAny = obj
  }

  guard let root = rootAny as? [String: Any] else {
    error = "配置根节点必须是对象"
    return nil
  }

  var config = AppConfig()
  if let versionAny = root["schemaVersion"] {
    // 没有版本号的早期配置按版本 1 兼容；显式版本必须准确匹配。
    // 不能把字符串、null、布尔或小数回退/截断为 1，否则重写配置时会丢失未知字段。
    guard exactVersionOne(versionAny) else {
      error = "不支持的配置 schemaVersion"
      return nil
    }
    config.schemaVersion = 1
  }
  if let r = intFromAny(root["revision"]) { config.revision = max(1, r) }
  if let s = root["selectedTemplateId"] as? String { config.selectedTemplateId = s }

  if let templatesAny = root["templates"] {
    guard let list = templatesAny as? [Any] else {
      error = "templates 必须是数组"
      return nil
    }
    for item in list {
      switch parseScheduleObject(item) {
      case .success(let schedule): config.templates.append(schedule)
      case .failure(let err):
        error = err.message
        return nil
      }
    }
  }

  if let overridesDict = root["dailyOverrides"] as? [String: Any] {
    for (dateKey, entryAny) in overridesDict {
      guard let entryDict = entryAny as? [String: Any],
            let segmentsAny = entryDict["segments"] else { continue }
      switch parseSegmentsArray(segmentsAny) {
      case .success(let segments):
        config.dailyOverrides[dateKey] = DailyOverride(segments: segments)
      case .failure(let err):
        error = err.message
        return nil
      }
    }
  }

  if let pres = root["presentation"] as? [String: Any] {
    if let v = pres["view"] as? String {
      if v == "screen_edge" {
        config.presentation.view = .screenEdge
      } else if v == "ring" {
        config.presentation.view = .ring
      } else {
        config.presentation.view = .bar
      }
    }
    if let s = pres["scope"] as? String {
      config.presentation.scope = (s == "current_block") ? .currentBlock : .allDay
    }
    if let e = pres["edge"] as? String {
      config.presentation.edge = PresentationSettings.Edge(rawValue: e) ?? .bottom
    }
    if let mb = pres["menuBarDisplay"] as? String {
      config.presentation.menuBarDisplay = PresentationSettings.MenuBarDisplayMode(rawValue: mb) ?? .countdown
    }
    if let gs = pres["glassStyle"] as? String {
      config.presentation.glassStyle = PresentationSettings.GlassStyle(rawValue: gs) ?? .clear
    }
    if let vis = pres["visible"] {
      config.presentation.visible = boolFromAny(vis, fallback: true)
    }
    if let autoHide = pres["autoHideAtEdge"] {
      config.presentation.autoHideAtEdge = boolFromAny(autoHide, fallback: false)
    }
    if let flush = pres["flushWithEdge"] {
      config.presentation.flushWithEdge = boolFromAny(flush, fallback: true)
    }
    if let rawThickness = intFromAny(pres["screenEdgeThickness"]) {
      config.presentation.screenEdgeThickness = min(
        PresentationSettings.maxScreenEdgeThickness,
        max(PresentationSettings.minScreenEdgeThickness, rawThickness)
      )
    }
    if let colors = pres["screenEdgeColors"] as? String {
      config.presentation.screenEdgeColors = (colors == "current_state")
        ? .currentState
        : .schedule
    }
    if let rawBarLength = intFromAny(pres["barLengthDip"]) {
      config.presentation.barLengthDip = (rawBarLength == PresentationSettings.defaultBarLength)
        ? rawBarLength
        : min(PresentationSettings.maxBarLength, max(PresentationSettings.minBarLength, rawBarLength))
    }
    if let rawSnap = intFromAny(pres["timelineSnapMinutes"]) {
      if rawSnap == 1 || rawSnap == 5 || rawSnap == 15 {
        config.presentation.timelineSnapMinutes = rawSnap
      }
    }
    if let bg = pres["barBackground"] as? String {
      // 只接受合法的 RRGGBB；无效值按“跟随系统”处理，不让整份配置失败。
      if isValidHexColor(bg) {
        config.presentation.barBackground = bg
      }
    }
    if let monitor = pres["monitorId"] as? String {
      config.presentation.monitorId = monitor
    }
    if let ox = intFromAny(pres["offsetX"]) { config.presentation.offsetX = ox }
    if let oy = intFromAny(pres["offsetY"]) { config.presentation.offsetY = oy }
  }

  if let notif = root["notifications"] as? [String: Any] {
    if let v = notif["enabled"] { config.notifications.enabled = boolFromAny(v, fallback: true) }
    if let v = notif["muted"] { config.notifications.muted = boolFromAny(v, fallback: false) }
    if let v = notif["focusBoundaries"] {
      config.notifications.focusBoundaries = boolFromAny(v, fallback: true)
    }
    if let v = notif["restBoundaries"] {
      config.notifications.restBoundaries = boolFromAny(v, fallback: true)
    }
    if let v = notif["lunchBoundaries"] {
      config.notifications.lunchBoundaries = boolFromAny(v, fallback: true)
    }
  }

  if let launch = root["launchAtLogin"] {
    config.launchAtLogin = boolFromAny(launch, fallback: false)
  }

  if config.templates.isEmpty {
    error = "配置中没有任何作息模板"
    return nil
  }
  if findTemplate(config, id: config.selectedTemplateId) == nil {
    config.selectedTemplateId = config.templates[0].id
  }
  return config
}

public func parseConfig(_ text: String) -> AppConfig? {
  var ignored = ""
  return parseConfig(text, error: &ignored)
}

public func serializeConfig(_ config: AppConfig) -> String {
  var overrides: [String: Any] = [:]
  for (key, value) in config.dailyOverrides {
    overrides[key] = ["segments": value.segments.map(segmentToObject)]
  }

  let presentation: [String: Any] = [
    "view": config.presentation.view.rawValue,
    "scope": config.presentation.scope.rawValue,
    "edge": config.presentation.edge.rawValue,
    "menuBarDisplay": config.presentation.menuBarDisplay.rawValue,
    "glassStyle": config.presentation.glassStyle.rawValue,
    "visible": config.presentation.visible,
    "autoHideAtEdge": config.presentation.autoHideAtEdge,
    "flushWithEdge": config.presentation.flushWithEdge,
    "screenEdgeThickness": config.presentation.screenEdgeThickness,
    "screenEdgeColors": config.presentation.screenEdgeColors.rawValue,
    "barLengthDip": config.presentation.barLengthDip,
    "timelineSnapMinutes": config.presentation.timelineSnapMinutes,
    "barBackground": config.presentation.barBackground,
    "monitorId": config.presentation.monitorId,
    "offsetX": config.presentation.offsetX,
    "offsetY": config.presentation.offsetY,
  ]

  let notifications: [String: Any] = [
    "enabled": config.notifications.enabled,
    "muted": config.notifications.muted,
    "focusBoundaries": config.notifications.focusBoundaries,
    "restBoundaries": config.notifications.restBoundaries,
    "lunchBoundaries": config.notifications.lunchBoundaries,
  ]

  let root: [String: Any] = [
    "schemaVersion": config.schemaVersion,
    "revision": config.revision,
    "selectedTemplateId": config.selectedTemplateId,
    "templates": config.templates.map(scheduleToObject),
    "dailyOverrides": overrides,
    "presentation": presentation,
    "notifications": notifications,
    "launchAtLogin": config.launchAtLogin,
  ]
  return dumpJson(root, pretty: true)
}

public func findTemplate(_ config: AppConfig, id: String) -> Schedule? {
  config.templates.first { $0.id == id }
}

public func selectedTemplate(_ config: AppConfig) -> Schedule? {
  findTemplate(config, id: config.selectedTemplateId)
}

// 今日覆盖优先；否则按星期取常规模板；无安排返回空。
public func effectiveSegments(_ config: AppConfig, now: WallClock) -> (segments: [Segment], fromOverride: Bool) {
  if let override = config.dailyOverrides[now.dateKey] {
    return (override.segments, true)
  }
  guard let schedule = selectedTemplate(config), schedule.isActive(on: now.weekday) else {
    return ([], false)
  }
  return (schedule.segments, false)
}

public func hasOverride(_ config: AppConfig, dateKey: String) -> Bool {
  config.dailyOverrides[dateKey] != nil
}

public func setOverride(_ config: inout AppConfig, dateKey: String, segments: [Segment]) {
  config.dailyOverrides[dateKey] = DailyOverride(segments: segments)
}

public func removeOverride(_ config: inout AppConfig, dateKey: String) {
  config.dailyOverrides.removeValue(forKey: dateKey)
}

public func pruneOverrides(_ config: inout AppConfig, keepFromDate: String) {
  config.dailyOverrides = config.dailyOverrides.filter { $0.key >= keepFromDate }
}

public func bumpRevision(_ config: inout AppConfig) {
  if config.revision < 2_147_483_000 {
    config.revision += 1
  } else {
    config.revision = 1
  }
}

// —— 运行态（通知去重）——

public func serializeRuntimeState(_ state: RuntimeState) -> String {
  let root: [String: Any] = [
    "firedKeys": state.firedKeys,
    "lastActiveDate": state.lastActiveDate,
  ]
  return dumpJson(root, pretty: true)
}

public func parseRuntimeState(_ text: String) -> RuntimeState {
  var state = RuntimeState()
  guard case .success(let anyObj) = decodeJsonObject(text),
        let dict = anyObj as? [String: Any] else {
    return state
  }
  if let keys = dict["firedKeys"] as? [Any] {
    state.firedKeys = keys.compactMap { $0 as? String }
  }
  if let date = dict["lastActiveDate"] as? String {
    state.lastActiveDate = date
  }
  return state
}

public func hasFired(_ state: RuntimeState, key: String) -> Bool {
  state.firedKeys.contains(key)
}

public func markFired(_ state: inout RuntimeState, key: String, cap: Int = 64) {
  if !hasFired(state, key: key) {
    state.firedKeys.append(key)
  }
  while state.firedKeys.count > cap {
    state.firedKeys.removeFirst()
  }
}
