// 应用级状态中心：配置、今日覆盖、当前快照与持久化。
// UI 与后台边界线程共享同一份有效作息数据，通过锁保证跨线程一致性。
import AppKit
import ClassTimeEngine
import Combine
import Foundation
import ServiceManagement

enum SaveScope: Int, CaseIterable {
  case todayOnly = 0
  case permanent = 1
}

struct PlanSnapshot: Sendable {
  var segments: [Segment]
  var revision: Int
  var dateKey: String
}

func nowClock() -> WallClock {
  let calendar = Calendar(identifier: .gregorian)
  let comps = calendar.dateComponents(
    in: .current,
    from: Date()
  )
  return WallClock(
    year: comps.year ?? 1970,
    month: comps.month ?? 1,
    day: comps.day ?? 1,
    hour: comps.hour ?? 0,
    minute: comps.minute ?? 0,
    second: comps.second ?? 0
  )
}

final class Services: ObservableObject, @unchecked Sendable {
  static let shared = Services()

  enum ChangeKind {
    case all
    case presentation
  }

  private let lock = NSLock()
  private var configStorage = AppConfig()
  private var runtimeStorage = RuntimeState()
  private var effectiveSegmentsStorage: [Segment] = []
  private var snapshotStorage = Snapshot()
  private var usingOverrideStorage = false
  private var isInitialized = false
  private var dataDirURL: URL = FileManager.default.temporaryDirectory
  private var changeListeners: [UUID: (ChangeKind) -> Void] = [:]

  @Published private(set) var uiRevision: Int = 0
  @Published private(set) var launchAtLoginError: String = ""
  @Published private(set) var launchAtLoginRequiresApproval: Bool = false

  private init() {}

  static func resolveDataDirectory() -> URL {
    let env = ProcessInfo.processInfo.environment
    if let custom = env["CLASSTIME_DATA_DIR"], !custom.isEmpty {
      return URL(fileURLWithPath: custom, isDirectory: true)
    } else if let appSupport = FileManager.default.urls(
      for: .applicationSupportDirectory,
      in: .userDomainMask
    ).first {
      return appSupport.appendingPathComponent("ClassTime", isDirectory: true)
    } else {
      return FileManager.default.temporaryDirectory.appendingPathComponent(
        "ClassTime",
        isDirectory: true
      )
    }
  }

  /// 无法安全读取、备份或写入配置时返回 false，绝不覆写原文件或已有备份。
  @discardableResult
  func initialize() -> Bool {
    lock.lock()
    if isInitialized {
      lock.unlock()
      return true
    }

    dataDirURL = Self.resolveDataDirectory()
    do {
      try FileManager.default.createDirectory(at: dataDirURL, withIntermediateDirectories: true)
    } catch {
      lock.unlock()
      return false
    }

    let rtURL = dataDirURL.appendingPathComponent("runtime-state.json")
    if let rtText = try? String(contentsOf: rtURL, encoding: .utf8) {
      runtimeStorage = parseRuntimeState(rtText)
    }

    let cfgURL = dataDirURL.appendingPathComponent("config.json")
    var loaded = false
    if FileManager.default.fileExists(atPath: cfgURL.path) {
      if let text = try? String(contentsOf: cfgURL, encoding: .utf8),
         let parsed = parseConfig(text) {
        configStorage = parsed
        loaded = true
      } else if !backupCorruptFile(cfgURL) {
        // 在原文件得到可靠备份之前，绝不能写入默认配置。
        lock.unlock()
        return false
      }
    }

    if !loaded {
      let presets = loadBundledPresets()
      var fresh = AppConfig()
      fresh.templates = presets
      fresh.selectedTemplateId = presets.first?.id ?? "custom"
      fresh.revision = 1
      configStorage = fresh
    }

    let today = nowClock().dateKey
    pruneOverrides(&configStorage, keepFromDate: today)
    bumpRevision(&configStorage)
    recomputeLocked()
    guard persistConfigLocked() else {
      lock.unlock()
      return false
    }
    isInitialized = true
    lock.unlock()

    refreshLaunchAtLoginStatus()
    notifyChanged(.all)
    return true
  }

  // —— 只读快照 ——

  var config: AppConfig {
    lock.lock()
    defer { lock.unlock() }
    return configStorage
  }

  var snapshot: Snapshot {
    lock.lock()
    defer { lock.unlock() }
    return snapshotStorage
  }

  var currentEffectiveSegments: [Segment] {
    lock.lock()
    defer { lock.unlock() }
    return effectiveSegmentsStorage
  }

  var usingTodayOverride: Bool {
    lock.lock()
    defer { lock.unlock() }
    return usingOverrideStorage
  }

  var todayKey: String {
    nowClock().dateKey
  }

  var dataDirectory: URL {
    lock.lock()
    defer { lock.unlock() }
    return dataDirURL
  }

  var configPath: URL {
    dataDirectory.appendingPathComponent("config.json")
  }

  var runtimePath: URL {
    dataDirectory.appendingPathComponent("runtime-state.json")
  }

  func visibleSegments(at clock: WallClock = nowClock()) -> [Segment] {
    lock.lock()
    let segments = effectiveSegmentsStorage
    let scope: Scope = (configStorage.presentation.scope == .allDay) ? .allDay : .currentBlock
    lock.unlock()

    let indices = visibleSegmentIndices(segments, now: clock, scope: scope)
    return indices.compactMap { $0 < segments.count ? segments[$0] : nil }
  }

  func plan(for now: WallClock) -> PlanSnapshot {
    lock.lock()
    defer { lock.unlock() }
    let eff = effectiveSegments(configStorage, now: now)
    return PlanSnapshot(
      segments: eff.segments,
      revision: configStorage.revision,
      dateKey: now.dateKey
    )
  }

  // —— 订阅 ——

  @discardableResult
  func addObserver(_ handler: @escaping (ChangeKind) -> Void) -> UUID {
    lock.lock()
    defer { lock.unlock() }
    let token = UUID()
    changeListeners[token] = handler
    return token
  }

  func removeObserver(_ token: UUID) {
    lock.lock()
    defer { lock.unlock() }
    changeListeners.removeValue(forKey: token)
  }

  // —— 变更操作 ——

  @discardableResult
  func applyScheduleEdit(_ segments: [Segment], scope: SaveScope, error: inout String) -> Bool {
    error = ""
    guard validateSegments(segments) else {
      error = "时段存在重叠或时间不合法"
      return false
    }

    lock.lock()
    var next = configStorage
    let today = nowClock().dateKey
    switch scope {
    case .todayOnly:
      setOverride(&next, dateKey: today, segments: segments)
    case .permanent:
      guard let idx = next.templates.firstIndex(where: { $0.id == next.selectedTemplateId }) else {
        lock.unlock()
        error = "找不到当前模板"
        return false
      }
      // 只改常规模板；今日覆盖不会在这里被悄悄改写。
      next.templates[idx].segments = segments
    }
    bumpRevision(&next)
    configStorage = next
    persistConfigLocked()
    recomputeLocked()
    persistRuntimeLocked()
    lock.unlock()

    notifyChanged(.all)
    return true
  }

  func updatePresentation(_ presentation: PresentationSettings) {
    lock.lock()
    configStorage.presentation = presentation
    persistConfigLocked()
    recomputeLocked()
    lock.unlock()
    notifyChanged(.presentation)
  }

  func updateNotifications(_ notifications: NotificationSettings) {
    lock.lock()
    configStorage.notifications = notifications
    persistConfigLocked()
    lock.unlock()
    notifyChanged(.all)
  }

  func setMuted(_ muted: Bool) {
    lock.lock()
    if configStorage.notifications.muted == muted {
      lock.unlock()
      return
    }
    configStorage.notifications.muted = muted
    persistConfigLocked()
    lock.unlock()
    notifyChanged(.all)
  }

  func refreshLaunchAtLoginStatus() {
    guard Bundle.main.bundleURL.pathExtension == "app" else { return }
    let status = SMAppService.mainApp.status
    let systemEnabled = (status == .enabled || status == .requiresApproval)
    let needsApproval = (status == .requiresApproval)

    lock.lock()
    if configStorage.launchAtLogin != systemEnabled {
      configStorage.launchAtLogin = systemEnabled
      _ = persistConfigLocked()
    }
    lock.unlock()

    DispatchQueue.main.async {
      self.launchAtLoginRequiresApproval = needsApproval
      if needsApproval {
        self.launchAtLoginError = "需要在「系统设置 → 通用 → 登录项与扩展」中批准 It's Class Time!。"
      } else if self.launchAtLoginError.contains("批准") {
        self.launchAtLoginError = ""
      }
      self.uiRevision &+= 1
    }
  }

  func openSystemLoginItemsSettings() {
    SMAppService.openSystemSettingsLoginItems()
  }

  func setLaunchAtLogin(_ enabled: Bool) {
    var statusMessage = ""
    var applied = enabled
    var needsApproval = false

    if Bundle.main.bundleURL.pathExtension == "app" {
      do {
        if enabled {
          try SMAppService.mainApp.register()
        } else {
          try SMAppService.mainApp.unregister()
        }
        let status = SMAppService.mainApp.status
        applied = (status == .enabled || status == .requiresApproval)
        needsApproval = (status == .requiresApproval)
        if needsApproval {
          statusMessage = "已请求登录启动，请在「系统设置 → 通用 → 登录项与扩展」中批准。"
        }
      } catch {
        let status = SMAppService.mainApp.status
        applied = (status == .enabled || status == .requiresApproval)
        needsApproval = (status == .requiresApproval)
        statusMessage = "系统登录项更新提示：\(error.localizedDescription)"
      }
    } else if enabled {
      applied = false
      statusMessage = "当前以命令行模式运行，打包为 .app 并移入应用程序目录后登录启动生效。"
    }

    lock.lock()
    configStorage.launchAtLogin = applied
    _ = persistConfigLocked()
    lock.unlock()

    DispatchQueue.main.async {
      self.launchAtLoginRequiresApproval = needsApproval
      self.launchAtLoginError = statusMessage
    }
    notifyChanged(.all)
  }

  @discardableResult
  func addTemplate(name: String, copyCurrent: Bool, error: inout String) -> Bool {
    error = ""
    lock.lock()
    var next = configStorage
    guard createTemplate(&next, name: name, copyCurrent: copyCurrent, error: &error) else {
      lock.unlock()
      return false
    }
    let cfgURL = dataDirURL.appendingPathComponent("config.json")
    guard writeAtomic(url: cfgURL, content: serializeConfig(next)) else {
      lock.unlock()
      error = "无法保存配置，请检查配置文件夹的写入权限"
      return false
    }
    configStorage = next
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
    return true
  }

  func renameTemplate(id: String, name: String) {
    let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !trimmed.isEmpty else { return }
    lock.lock()
    guard let idx = configStorage.templates.firstIndex(where: { $0.id == id }) else {
      lock.unlock()
      return
    }
    if configStorage.templates[idx].name == trimmed {
      lock.unlock()
      return
    }
    configStorage.templates[idx].name = trimmed
    bumpRevision(&configStorage)
    _ = persistConfigLocked()
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
  }

  @discardableResult
  func selectTemplate(_ templateId: String, error: inout String) -> Bool {
    error = ""
    lock.lock()
    guard findTemplate(configStorage, id: templateId) != nil else {
      lock.unlock()
      error = "模板不存在"
      return false
    }
    if configStorage.selectedTemplateId == templateId {
      lock.unlock()
      return true
    }
    configStorage.selectedTemplateId = templateId
    bumpRevision(&configStorage)
    persistConfigLocked()
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
    return true
  }

  func setActiveWeekdays(_ weekdays: [Int]) {
    lock.lock()
    guard let idx = configStorage.templates.firstIndex(
      where: { $0.id == configStorage.selectedTemplateId }
    ) else {
      lock.unlock()
      return
    }
    let uniqueSorted = Array(Set(weekdays.filter { (1...7).contains($0) })).sorted()
    configStorage.templates[idx].activeWeekdays = uniqueSorted
    bumpRevision(&configStorage)
    persistConfigLocked()
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
  }

  @discardableResult
  func restorePreset(error: inout String) -> Bool {
    error = ""
    let presets = loadBundledPresets()
    lock.lock()
    guard let idx = configStorage.templates.firstIndex(
      where: { $0.id == configStorage.selectedTemplateId }
    ) else {
      lock.unlock()
      error = "当前模板不存在"
      return false
    }
    let targetId = configStorage.selectedTemplateId
    guard let match = presets.first(where: { $0.id == targetId }) else {
      lock.unlock()
      error = "这是自定义模板，没有可恢复的随包预设"
      return false
    }
    configStorage.templates[idx].segments = match.segments
    configStorage.templates[idx].name = match.name
    configStorage.templates[idx].activeWeekdays = match.activeWeekdays
    bumpRevision(&configStorage)
    persistConfigLocked()
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
    return true
  }

  func clearTodayOverride() {
    lock.lock()
    removeOverride(&configStorage, dateKey: nowClock().dateKey)
    bumpRevision(&configStorage)
    persistConfigLocked()
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
  }

  func refresh() {
    lock.lock()
    pruneOverrides(&configStorage, keepFromDate: nowClock().dateKey)
    recomputeLocked()
    lock.unlock()

    notifyChanged(.all)
  }

  func tickSnapshotOnly() {
    lock.lock()
    recomputeLocked()
    lock.unlock()
    DispatchQueue.main.async {
      self.uiRevision &+= 1
    }
  }

  func save() {
    lock.lock()
    persistConfigLocked()
    persistRuntimeLocked()
    lock.unlock()
  }

  // —— 通知去重 ——

  func wasNotified(_ key: String) -> Bool {
    lock.lock()
    defer { lock.unlock() }
    return hasFired(runtimeStorage, key: key)
  }

  func markNotified(_ key: String) {
    lock.lock()
    markFired(&runtimeStorage, key: key)
    persistRuntimeLocked()
    lock.unlock()
  }

  func forgetNotificationsForOtherDays(_ dateKey: String) {
    lock.lock()
    runtimeStorage.firedKeys = runtimeStorage.firedKeys.filter { $0.hasPrefix(dateKey) }
    runtimeStorage.lastActiveDate = dateKey
    persistRuntimeLocked()
    lock.unlock()
  }

  // —— 内部辅助 ——

  private func recomputeLocked() {
    let now = nowClock()
    let eff = effectiveSegments(configStorage, now: now)
    effectiveSegmentsStorage = eff.segments
    usingOverrideStorage = eff.fromOverride
    snapshotStorage = evaluate(eff.segments, now: now)
  }

  @discardableResult
  private func persistConfigLocked() -> Bool {
    let url = dataDirURL.appendingPathComponent("config.json")
    return writeAtomic(url: url, content: serializeConfig(configStorage))
  }

  @discardableResult
  private func persistRuntimeLocked() -> Bool {
    let url = dataDirURL.appendingPathComponent("runtime-state.json")
    return writeAtomic(url: url, content: serializeRuntimeState(runtimeStorage))
  }

  @discardableResult
  private func writeAtomic(url: URL, content: String) -> Bool {
    do {
      try FileManager.default.createDirectory(
        at: url.deletingLastPathComponent(),
        withIntermediateDirectories: true
      )
      guard let data = content.data(using: .utf8) else { return false }
      try data.write(to: url, options: [.atomic])
      return true
    } catch {
      return false
    }
  }

  private func backupCorruptFile(_ url: URL) -> Bool {
    let c = nowClock()
    let suffix = String(
      format: ".corrupt-%04d%02d%02d-%02d%02d%02d",
      c.year, c.month, c.day, c.hour, c.minute, c.second
    )
    // 同一秒内多次恢复时，绝不覆盖之前保留下来的备份。
    for attempt in 0..<1000 {
      let extra = attempt == 0 ? "" : "-\(attempt)"
      let backupURL = URL(fileURLWithPath: url.path + suffix + extra)
      if FileManager.default.fileExists(atPath: backupURL.path) {
        continue
      }
      do {
        try FileManager.default.copyItem(at: url, to: backupURL)
        return true
      } catch let err as NSError where err.domain == NSCocoaErrorDomain && err.code == NSFileWriteFileExistsError {
        continue
      } catch {
        return false
      }
    }
    return false
  }

  private func loadBundledPresets() -> [Schedule] {
    var candidateDirs: [URL] = []
    if let resURL = Bundle.main.resourceURL {
      candidateDirs.append(resURL.appendingPathComponent("presets", isDirectory: true))
    }
    let execDir = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
    candidateDirs.append(execDir.appendingPathComponent("presets", isDirectory: true))
    // 兼容开发态直接 swift run
    var probe = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    for _ in 0..<4 {
      candidateDirs.append(probe.appendingPathComponent("shared/presets", isDirectory: true))
      probe.deleteLastPathComponent()
    }

    var out: [Schedule] = []
    for dir in candidateDirs {
      var found: [Schedule] = []
      for name in ["high-school.json", "university.json"] {
        let file = dir.appendingPathComponent(name)
        guard let text = try? String(contentsOf: file, encoding: .utf8),
              let parsed = parsePreset(text) else { continue }
        found.append(parsed)
      }
      if !found.isEmpty {
        out = found
        break
      }
    }

    if out.isEmpty {
      out.append(
        Schedule(
          schemaVersion: 1,
          id: "custom",
          name: "空白模板",
          activeWeekdays: [1, 2, 3, 4, 5],
          segments: []
        )
      )
    }
    return out
  }

  private func notifyChanged(_ kind: ChangeKind) {
    lock.lock()
    let listeners = Array(changeListeners.values)
    lock.unlock()

    DispatchQueue.main.async {
      self.uiRevision &+= 1
      for listener in listeners {
        listener(kind)
      }
    }
  }
}

// —— 中文界面格式化工具 ——

enum Formatters {
  static func dayStateLabel(_ state: DayState) -> String {
    switch state {
    case .idle: return "空闲"
    case .focus: return "上课中"
    case .shortBreak: return "课间"
    case .longBreak: return "长休息"
    case .lunch: return "午休"
    }
  }

  static func segmentKindLabel(_ kind: SegmentKind) -> String {
    switch kind {
    case .focus: return "上课"
    case .shortBreak: return "课间"
    case .longBreak: return "长休息"
    case .lunch: return "午休"
    }
  }

  static func clock(_ time: TimeOfDay) -> String {
    time.descriptionText
  }

  static func range(_ segment: Segment) -> String {
    "\(segment.start.descriptionText)–\(segment.end.descriptionText)"
  }

  static func range(_ segment: SegmentRef) -> String {
    "\(segment.start.descriptionText)–\(segment.end.descriptionText)"
  }

  static func minutes(_ m: Int) -> String {
    "\(m) 分钟"
  }

  static func percent(_ progress: Double) -> String {
    "\(Int((progress * 100.0).rounded()))%"
  }

  static func elapsed(_ seconds: Int) -> String {
    let clamped = max(0, seconds)
    return String(format: "%02d:%02d", clamped / 60, clamped % 60)
  }

  static func remainingClock(_ seconds: Int) -> String {
    let clamped = max(0, seconds)
    if clamped < 3600 {
      return elapsed(clamped)
    }
    return "\(clamped / 3600):\(elapsed(clamped % 3600))"
  }

  static func weekdayLabel(_ isoWeekday: Int) -> String {
    switch isoWeekday {
    case 1: return "周一"
    case 2: return "周二"
    case 3: return "周三"
    case 4: return "周四"
    case 5: return "周五"
    case 6: return "周六"
    case 7: return "周日"
    default: return "周\(isoWeekday)"
    }
  }

  static func statusSummary(snapshot: Snapshot, muted: Bool) -> String {
    var parts: [String] = ["“上课”时间到！"]
    if let current = snapshot.current {
      if current.kind == .focus {
        parts.append("第 \(current.focusOrdinal) 节 \(percent(snapshot.progress))")
      } else {
        parts.append("\(segmentKindLabel(current.kind)) \(percent(snapshot.progress))")
      }
    } else if let next = snapshot.next {
      parts.append("下一段 \(clock(next.start))")
    } else if snapshot.hasSchedule {
      parts.append("今日已结束")
    } else {
      parts.append("今天没有安排")
    }
    if muted {
      parts.append("提醒已暂停")
    }
    return parts.joined(separator: " · ")
  }
}
