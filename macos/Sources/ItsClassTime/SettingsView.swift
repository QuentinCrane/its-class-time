// SwiftUI 原生设置界面（遵循 Apple macOS HIG 与 Liquid Glass 规范，全量对齐 Windows 功能）：
// - 作息计划：自定义模板新增与重命名、常规模板/今日计划隔离、分组时段列表、复制时段、可缩放拖动时间轴、周/日课表网格、今日概览
// - 显示与位置：顶部固定实时预览（近景+桌面示意）、三形态选择、多显示器选择、长条长度与原生 ColorPicker 自定义背景色、屏幕边框全天分色与粗细、贴边行为
// - 通知：有效状态摘要、对齐开关行、系统通知权限检查与即时测试
// - 常规：SMAppService 登录启动与批准引导、本地数据目录、版本信息与退出应用
// 注：遵循仓库 Command Line Tools 兼容规范，视图状态统一使用 ObservableObject + @ObservedObject。
import AppKit
import ClassTimeEngine
import SwiftUI

enum SettingsSection: String, CaseIterable, Identifiable {
  case schedule
  case display
  case notifications
  case general

  var id: String { rawValue }

  var title: String {
    switch self {
    case .schedule: return "作息计划"
    case .display: return "显示与位置"
    case .notifications: return "通知"
    case .general: return "常规"
    }
  }

  var icon: String {
    switch self {
    case .schedule: return "calendar.badge.clock"
    case .display: return "menubar.dock.rectangle"
    case .notifications: return "bell.badge"
    case .general: return "gearshape"
    }
  }
}

private let appVersionString = "0.1.2"

// MARK: - 新增模板 Sheet 状态

@MainActor
final class AddTemplateViewModel: ObservableObject, Identifiable {
  let id = UUID()
  @Published var name: String = "新模板"
  @Published var copyCurrent: Bool = true
  @Published var errorText: String = ""

  let onCreated: (String) -> Void
  let onCancel: () -> Void

  init(onCreated: @escaping (String) -> Void, onCancel: @escaping () -> Void) {
    self.onCreated = onCreated
    self.onCancel = onCancel
  }

  func submit() {
    let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !trimmed.isEmpty else {
      errorText = "请输入模板名称。"
      return
    }
    var err = ""
    guard Services.shared.addTemplate(name: trimmed, copyCurrent: copyCurrent, error: &err) else {
      errorText = "创建模板失败：\(err)"
      return
    }
    let msg = Services.shared.usingTodayOverride
      ? "新模板已创建；今天仍按临时计划执行。"
      : "新模板已创建并开始使用。"
    onCreated(msg)
  }
}

// MARK: - 设置主 ViewModel

@MainActor
final class SettingsViewModel: ObservableObject {
  @Published var selectedSection: SettingsSection? = .schedule {
    didSet {
      syncPreviewTimer()
    }
  }
  @Published var viewingTodayPlan: Bool = false
  @Published var noticeText: String = ""
  @Published var noticeIsError: Bool = false
  @Published var activeEditor: SegmentEditorViewModel?
  @Published var addTemplateSheet: AddTemplateViewModel?
  @Published var showRestoreConfirm: Bool = false
  @Published var previewHovered: Bool = false
  @Published var testFeedback: String = "尚未发送测试通知。"
  @Published var permissionText: String = NotificationService.shared.permissionLabel
  @Published var systemNotificationsEnabled: Bool = NotificationService.shared.isSystemNotificationEnabled

  // 展开区状态
  @Published var showTemplateRename: Bool = false
  @Published var showTimeline: Bool = false
  @Published var showTimetable: Bool = false
  @Published var showTodayOverview: Bool = false

  // 模板重命名防抖
  @Published var templateNameDraft: String = ""
  private var templateRenameTargetId: String = ""
  private var templateRenameBase: String = ""
  private var templateRenameWorkItem: DispatchWorkItem?

  // 显示页防抖与实时预览状态
  @Published var displays: [DisplayChoice] = []
  @Published var previewNow: WallClock = nowClock()
  @Published var pendingBarLength: Int? = nil
  @Published var pendingScreenEdgeThickness: Int? = nil
  @Published var pendingBarBackground: String? = nil

  private var barLengthWorkItem: DispatchWorkItem?
  private var thicknessWorkItem: DispatchWorkItem?
  private var barBgWorkItem: DispatchWorkItem?
  private var isWindowVisible: Bool = false
  private var previewTimer: Timer?

  let timelineModel = TimelineEditorViewModel()

  init() {
    displays = DisplayCatalog.enumerateDisplays()
    syncDraftsFromConfig()
  }

  func setWindowVisible(_ visible: Bool) {
    isWindowVisible = visible
    syncPreviewTimer()
  }

  func refreshDisplays() {
    displays = DisplayCatalog.enumerateDisplays()
  }

  func syncDraftsFromConfig() {
    let cfg = Services.shared.config
    if let tpl = selectedTemplate(cfg) {
      if templateRenameTargetId != tpl.id {
        templateRenameTargetId = tpl.id
        templateRenameBase = tpl.name
        templateNameDraft = tpl.name
      } else if templateRenameWorkItem == nil && templateNameDraft != tpl.name {
        templateRenameBase = tpl.name
        templateNameDraft = tpl.name
      }
    }
  }

  var effectivePreviewPresentation: PresentationSettings {
    var pres = Services.shared.config.presentation
    if let len = pendingBarLength { pres.barLengthDip = len }
    if let t = pendingScreenEdgeThickness { pres.screenEdgeThickness = t }
    if let bg = pendingBarBackground { pres.barBackground = bg }
    return pres
  }

  // —— 预览定时刷新（仅在窗口可见且位于「显示与位置」页时启用，不参与提醒调度） ——

  private func syncPreviewTimer() {
    let shouldRun = isWindowVisible && (selectedSection == .display)
    if shouldRun {
      previewNow = nowClock()
      if previewTimer == nil {
        let timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
          Task { @MainActor in
            guard let self, self.isWindowVisible, self.selectedSection == .display else {
              self?.stopPreviewTimer()
              return
            }
            self.previewNow = nowClock()
          }
        }
        timer.tolerance = 0.2
        previewTimer = timer
      }
    } else {
      stopPreviewTimer()
    }
  }

  private func stopPreviewTimer() {
    previewTimer?.invalidate()
    previewTimer = nil
  }

  // —— 模板重命名防抖 ——

  func updateTemplateNameDraft(_ text: String) {
    templateNameDraft = text
    templateRenameWorkItem?.cancel()
    templateRenameWorkItem = nil

    let currentId = Services.shared.config.selectedTemplateId
    templateRenameTargetId = currentId
    let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
    if trimmed.isEmpty || trimmed == templateRenameBase { return }

    let item = DispatchWorkItem { [weak self] in
      self?.commitTemplateName()
    }
    templateRenameWorkItem = item
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: item)
  }

  func commitTemplateName() {
    templateRenameWorkItem?.cancel()
    templateRenameWorkItem = nil
    let trimmed = templateNameDraft.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !templateRenameTargetId.isEmpty, !trimmed.isEmpty, trimmed != templateRenameBase else {
      return
    }
    templateRenameBase = trimmed
    Services.shared.renameTemplate(id: templateRenameTargetId, name: trimmed)
  }

  // —— 长条长度 / 边框粗细 / 背景色防抖 ——

  func previewBarLength(_ value: Int) {
    pendingBarLength = value
    barLengthWorkItem?.cancel()
    let item = DispatchWorkItem { [weak self] in
      self?.commitBarLength()
    }
    barLengthWorkItem = item
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: item)
  }

  func commitBarLength() {
    barLengthWorkItem?.cancel()
    barLengthWorkItem = nil
    guard let value = pendingBarLength else { return }
    pendingBarLength = nil
    var pres = Services.shared.config.presentation
    guard pres.barLengthDip != value else { return }
    pres.barLengthDip = value
    Services.shared.updatePresentation(pres)
  }

  func resetBarLength() {
    barLengthWorkItem?.cancel()
    barLengthWorkItem = nil
    pendingBarLength = nil
    var pres = Services.shared.config.presentation
    pres.barLengthDip = PresentationSettings.defaultBarLength
    Services.shared.updatePresentation(pres)
  }

  func previewScreenEdgeThickness(_ value: Int) {
    pendingScreenEdgeThickness = value
    thicknessWorkItem?.cancel()
    let item = DispatchWorkItem { [weak self] in
      self?.commitScreenEdgeThickness()
    }
    thicknessWorkItem = item
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: item)
  }

  func commitScreenEdgeThickness() {
    thicknessWorkItem?.cancel()
    thicknessWorkItem = nil
    guard let value = pendingScreenEdgeThickness else { return }
    pendingScreenEdgeThickness = nil
    var pres = Services.shared.config.presentation
    guard pres.screenEdgeThickness != value else { return }
    pres.screenEdgeThickness = value
    Services.shared.updatePresentation(pres)
  }

  func previewBarBackground(_ hex: String) {
    pendingBarBackground = hex
    barBgWorkItem?.cancel()
    let item = DispatchWorkItem { [weak self] in
      self?.commitBarBackground()
    }
    barBgWorkItem = item
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.20, execute: item)
  }

  func commitBarBackground() {
    barBgWorkItem?.cancel()
    barBgWorkItem = nil
    guard let hex = pendingBarBackground else { return }
    pendingBarBackground = nil
    var pres = Services.shared.config.presentation
    guard pres.barBackground != hex else { return }
    pres.barBackground = hex
    Services.shared.updatePresentation(pres)
  }

  func setCustomBarBackgroundEnabled(_ enabled: Bool) {
    barBgWorkItem?.cancel()
    barBgWorkItem = nil
    pendingBarBackground = nil
    var pres = Services.shared.config.presentation
    if enabled {
      if pres.barBackground.isEmpty {
        pres.barBackground = WidgetPalette.systemBackgroundHex()
        Services.shared.updatePresentation(pres)
      }
    } else if !pres.barBackground.isEmpty {
      pres.barBackground = ""
      Services.shared.updatePresentation(pres)
    }
  }

  func flushPendingCommits() {
    commitTemplateName()
    commitBarLength()
    commitScreenEdgeThickness()
    commitBarBackground()
  }

  // —— 弹窗与编辑器入口 ——

  func openAddTemplate() {
    commitTemplateName()
    addTemplateSheet = AddTemplateViewModel(
      onCreated: { [weak self] message in
        self?.viewingTodayPlan = false
        self?.syncDraftsFromConfig()
        self?.noticeIsError = false
        self?.noticeText = message
        self?.addTemplateSheet = nil
      },
      onCancel: { [weak self] in
        self?.addTemplateSheet = nil
      }
    )
  }

  func openEditor(segment: Segment?, defaultPermanent: Bool) {
    let mode: SegmentEditorMode = segment.map { .edit($0) } ?? .create(startMinute: nil)
    presentEditor(mode: mode, defaultPermanent: defaultPermanent)
  }

  func openNewEditorAt(minute: Int, defaultPermanent: Bool) {
    presentEditor(mode: .create(startMinute: minute), defaultPermanent: defaultPermanent)
  }

  func openDuplicateEditor(segment: Segment, defaultPermanent: Bool) {
    presentEditor(mode: .duplicate(segment), defaultPermanent: defaultPermanent)
  }

  private func presentEditor(mode: SegmentEditorMode, defaultPermanent: Bool) {
    let scope: SaveScope = defaultPermanent ? .permanent : .todayOnly
    activeEditor = SegmentEditorViewModel(
      mode: mode,
      initialScope: scope,
      onSaved: { [weak self] message in
        self?.noticeIsError = false
        self?.noticeText = message
        self?.activeEditor = nil
      },
      onCancel: { [weak self] in
        self?.activeEditor = nil
      }
    )
  }

  func applyTimelineDragEdit(segmentId: String, startMinute: Int, endMinute: Int) {
    let permanent = !viewingTodayPlan
    let cfg = Services.shared.config
    var base: [Segment] = permanent
      ? (selectedTemplate(cfg)?.segments ?? [])
      : Services.shared.currentEffectiveSegments

    guard let idx = base.firstIndex(where: { $0.id == segmentId }) else {
      noticeIsError = true
      noticeText = "所选计划里没有这个时段，已恢复原时间。"
      return
    }
    guard let start = TimeOfDay.fromTotalMinutes(startMinute),
          let end = TimeOfDay.fromTotalMinutes(endMinute),
          end.totalMinutes > start.totalMinutes else {
      noticeIsError = true
      noticeText = "调整后的时间不合法，已恢复原时间。"
      return
    }
    base[idx].start = start
    base[idx].end = end
    let sorted = sortedByStart(base)
    guard validateSegments(sorted) else {
      noticeIsError = true
      noticeText = "调整后的时段与其他时段重叠，已恢复原时间。"
      return
    }
    var err = ""
    let scope: SaveScope = permanent ? .permanent : .todayOnly
    guard Services.shared.applyScheduleEdit(sorted, scope: scope, error: &err) else {
      noticeIsError = true
      noticeText = err
      return
    }
    noticeIsError = false
    if permanent {
      noticeText = Services.shared.usingTodayOverride
        ? "时间轴调整已保存到常规模板；今天仍按临时计划执行。"
        : "时间轴调整已保存到常规模板。"
    } else {
      noticeText = "时间轴调整已保存到今天的计划。"
    }
  }

  func syncPermissionState() {
    permissionText = NotificationService.shared.permissionLabel
    systemNotificationsEnabled = NotificationService.shared.isSystemNotificationEnabled
  }

  func refreshPermission() {
    NotificationService.shared.refreshAuthorizationStatus { [weak self] in
      DispatchQueue.main.async {
        self?.syncPermissionState()
      }
    }
  }
}

// MARK: - 时段编辑器 ViewModel（支持新增 / 复制 / 编辑，1 分钟精确输入绝不静默取整）

enum SegmentEditorMode {
  case create(startMinute: Int?)
  case duplicate(Segment)
  case edit(Segment)
}

@MainActor
final class SegmentEditorViewModel: ObservableObject, Identifiable {
  let id = UUID()
  let isNewSegment: Bool
  let titleText: String
  let segmentId: String
  let onSaved: (String) -> Void
  let onCancel: () -> Void

  @Published var scope: SaveScope
  @Published var label: String
  @Published var kind: SegmentKind
  @Published var startText: String
  @Published var endText: String
  @Published var durationText: String
  @Published var sliderValue: Double
  @Published var strategy: ShiftStrategy = .keep
  @Published var showDeleteConfirm: Bool = false
  @Published var saveErrorText: String = ""

  init(
    mode: SegmentEditorMode,
    initialScope: SaveScope,
    onSaved: @escaping (String) -> Void,
    onCancel: @escaping () -> Void
  ) {
    self.onSaved = onSaved
    self.onCancel = onCancel
    self.scope = initialScope

    switch mode {
    case .edit(let seg):
      self.isNewSegment = false
      self.titleText = "编辑时段"
      self.segmentId = seg.id
      self.label = seg.label
      self.kind = seg.kind
      self.startText = seg.start.descriptionText
      self.endText = seg.end.descriptionText
      let dur = max(1, seg.durationMinutes)
      self.durationText = "\(dur)"
      let maxVal = (seg.kind == .focus) ? 180.0 : 240.0
      self.sliderValue = min(maxVal, max(5.0, Double(dur)))

    case .duplicate(let seg):
      self.isNewSegment = true
      self.titleText = "复制时段"
      self.segmentId = "seg-\(Int(Date().timeIntervalSince1970 * 1000) % 1_000_000)-\(Int.random(in: 10...99))"
      self.label = seg.label
      self.kind = seg.kind
      let dur = min(1439, max(1, seg.durationMinutes))
      // 优先接在原时段之后；当天放不下则退回与原时段相同的起点。
      var startMin = seg.end.totalMinutes
      if startMin + dur > 1439 {
        startMin = seg.start.totalMinutes
      }
      startMin = min(max(0, startMin), 1439 - dur)
      let startTod = TimeOfDay.fromTotalMinutes(startMin) ?? TimeOfDay(hour: 8, minute: 0)
      let endTod = TimeOfDay.fromTotalMinutes(startMin + dur) ?? TimeOfDay(hour: 8, minute: 45)
      self.startText = startTod.descriptionText
      self.endText = endTod.descriptionText
      self.durationText = "\(dur)"
      let maxVal = (seg.kind == .focus) ? 180.0 : 240.0
      self.sliderValue = min(maxVal, max(5.0, Double(dur)))

    case .create(let requestedMinute):
      self.isNewSegment = true
      self.titleText = "新增时段"
      self.segmentId = "seg-\(Int(Date().timeIntervalSince1970 * 1000) % 1_000_000)-\(Int.random(in: 10...99))"
      let startMin: Int
      if let m = requestedMinute {
        startMin = min(max(0, m), 1438)
      } else {
        let base = sortedByStart(Services.shared.currentEffectiveSegments)
        startMin = min(base.last?.end.totalMinutes ?? (8 * 60), 23 * 60)
      }
      let dur = min(45, 1439 - startMin)
      let safeStart = TimeOfDay.fromTotalMinutes(startMin) ?? TimeOfDay(hour: 8, minute: 0)
      let safeEnd = TimeOfDay.fromTotalMinutes(startMin + dur) ?? TimeOfDay(hour: 8, minute: 45)
      self.label = ""
      self.kind = .focus
      self.startText = safeStart.descriptionText
      self.endText = safeEnd.descriptionText
      self.durationText = "\(dur)"
      self.sliderValue = Double(min(180, max(5, dur)))
    }
  }

  var sliderRange: ClosedRange<Double> {
    (kind == .focus) ? (5.0...180.0) : (5.0...240.0)
  }

  var parsedMinutes: Int? {
    let trimmed = durationText.trimmingCharacters(in: .whitespaces)
    guard !trimmed.isEmpty, trimmed.count <= 4, trimmed.allSatisfy(\.isNumber),
          let value = Int(trimmed), (1...1439).contains(value) else {
      return nil
    }
    return value
  }

  var baseSegments: [Segment] {
    let cfg = Services.shared.config
    if scope == .permanent {
      return sortedByStart(selectedTemplate(cfg)?.segments ?? [])
    }
    return sortedByStart(Services.shared.currentEffectiveSegments)
  }

  var existsInSelectedScope: Bool {
    !isNewSegment && baseSegments.contains(where: { $0.id == segmentId })
  }

  var draftSegment: Segment? {
    guard let start = TimeOfDay.parse(startText.trimmingCharacters(in: .whitespaces)),
          let end = TimeOfDay.parse(endText.trimmingCharacters(in: .whitespaces)),
          end.totalMinutes > start.totalMinutes else {
      return nil
    }
    let trimmedName = label.trimmingCharacters(in: .whitespacesAndNewlines)
    return Segment(
      id: segmentId,
      label: trimmedName.isEmpty ? "未命名时段" : trimmedName,
      kind: kind,
      start: start,
      end: end
    )
  }

  var draftEvaluation: EditOutcome {
    let trimmedName = label.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !trimmedName.isEmpty else {
      return EditOutcome(status: .invalidRange, message: "名称不能为空")
    }
    guard let start = TimeOfDay.parse(startText.trimmingCharacters(in: .whitespaces)) else {
      return EditOutcome(status: .invalidRange, message: "开始时间格式应为 HH:mm（00:00–23:59）")
    }
    guard let end = TimeOfDay.parse(endText.trimmingCharacters(in: .whitespaces)) else {
      return EditOutcome(status: .invalidRange, message: "结束时间格式应为 HH:mm（00:00–23:59）")
    }
    guard end.totalMinutes > start.totalMinutes else {
      return EditOutcome(status: .invalidRange, message: "结束时间必须晚于开始时间")
    }
    guard let mins = parsedMinutes, mins == end.totalMinutes - start.totalMinutes else {
      return EditOutcome(status: .invalidDuration, message: "请输入有效的整分钟时长，并确认结束时间在当天 23:59 之前。")
    }

    let base = baseSegments
    if !isNewSegment {
      guard base.contains(where: { $0.id == segmentId }) else {
        return EditOutcome(
          status: .notFound,
          message: "所选计划里没有这个时段。请选择原来的保存范围，或在目标计划中新增时段。"
        )
      }
      let req = EditRequest(
        segmentId: segmentId,
        start: start,
        end: end,
        durationMinutes: nil,
        label: trimmedName,
        kind: kind,
        strategy: strategy
      )
      return applyEdit(base, request: req)
    } else {
      let seg = Segment(id: segmentId, label: trimmedName, kind: kind, start: start, end: end)
      return insertSegment(base, segment: seg)
    }
  }

  var sliderHintText: String {
    let maxInt = Int(sliderRange.upperBound)
    guard let mins = parsedMinutes else {
      return "请输入 1–1439 之间的整数分钟；滑块每格 5 分钟（范围 5–\(maxInt) 分钟）。"
    }
    if mins < 5 || mins > maxInt || mins % 5 != 0 {
      return "将按 \(mins) 分钟保存。拖动滑块可按 5 分钟调整。"
    }
    return "滑块每格 5 分钟，数字框精确到 1 分钟。"
  }

  func updateKind(_ newKind: SegmentKind) {
    kind = newKind
    let maxVal = (newKind == .focus) ? 180.0 : 240.0
    if let mins = parsedMinutes {
      sliderValue = min(maxVal, max(5.0, Double(mins)))
    } else {
      sliderValue = min(maxVal, max(5.0, sliderValue))
    }
  }

  func updateMinutesText(_ newText: String) {
    durationText = newText
    guard let mins = parsedMinutes else { return }
    // 更新滑块视觉位置，绝不反写 durationText 以免把 121 静默取整成 120！
    let clamped = min(sliderRange.upperBound, max(sliderRange.lowerBound, Double(mins)))
    if abs(sliderValue - clamped) >= 0.5 {
      sliderValue = clamped
    }
    if let start = TimeOfDay.parse(startText.trimmingCharacters(in: .whitespaces)),
       let newEnd = TimeOfDay.fromTotalMinutes(start.totalMinutes + mins) {
      if endText != newEnd.descriptionText {
        endText = newEnd.descriptionText
      }
    }
  }

  func updateFromSlider(_ rawValue: Double) {
    let snapped = Int((rawValue / 5.0).rounded()) * 5
    sliderValue = Double(snapped)
    let newDurStr = "\(snapped)"
    if durationText != newDurStr {
      durationText = newDurStr
    }
    if let start = TimeOfDay.parse(startText.trimmingCharacters(in: .whitespaces)),
       let newEnd = TimeOfDay.fromTotalMinutes(start.totalMinutes + snapped) {
      if endText != newEnd.descriptionText {
        endText = newEnd.descriptionText
      }
    }
  }

  func updateStartText(_ newText: String) {
    startText = newText
    // 改变开始时间时，按用户输入的持续分钟重算结束时间（若结束时间无效或同步更新时长）
    guard let start = TimeOfDay.parse(newText.trimmingCharacters(in: .whitespaces)) else {
      return
    }
    if let mins = parsedMinutes, let newEnd = TimeOfDay.fromTotalMinutes(start.totalMinutes + mins) {
      if endText != newEnd.descriptionText {
        endText = newEnd.descriptionText
      }
    } else if let end = TimeOfDay.parse(endText.trimmingCharacters(in: .whitespaces)),
              end.totalMinutes > start.totalMinutes {
      let mins = end.totalMinutes - start.totalMinutes
      durationText = "\(mins)"
      sliderValue = min(sliderRange.upperBound, max(sliderRange.lowerBound, Double(mins)))
    }
  }

  func updateEndText(_ newText: String) {
    endText = newText
    guard let start = TimeOfDay.parse(startText.trimmingCharacters(in: .whitespaces)),
          let end = TimeOfDay.parse(newText.trimmingCharacters(in: .whitespaces)),
          end.totalMinutes > start.totalMinutes else {
      return
    }
    let mins = end.totalMinutes - start.totalMinutes
    let minsStr = "\(mins)"
    if durationText != minsStr {
      durationText = minsStr
    }
    sliderValue = min(sliderRange.upperBound, max(sliderRange.lowerBound, Double(mins)))
  }

  func conflictDescription(_ outcome: EditOutcome) -> String {
    guard !outcome.conflictIds.isEmpty else {
      return outcome.message
    }
    let base = baseSegments
    let names = outcome.conflictIds.map { cid -> String in
      if let found = base.first(where: { $0.id == cid }) {
        return "\(found.label)（\(Formatters.range(found))）"
      }
      return cid
    }
    return "\(outcome.message)（冲突：\(names.joined(separator: "、"))）"
  }

  func commitSave() {
    let evaluation = draftEvaluation
    guard evaluation.ok else { return }
    var err = ""
    let services = Services.shared
    let todayHadOverride = hasOverride(services.config, dateKey: services.todayKey)
    if services.applyScheduleEdit(evaluation.segments, scope: scope, error: &err) {
      let msg: String
      if scope == .todayOnly {
        msg = "今日计划已更新；常规模板未改变。"
      } else if todayHadOverride {
        msg = "模板已保存；今天已有临时调整，今日仍按临时方案执行。"
      } else {
        msg = "模板已保存，并会应用到设定的生效星期。"
      }
      onSaved(msg)
    } else {
      saveErrorText = err
    }
  }

  func performDelete() {
    let outcome = removeSegment(baseSegments, id: segmentId)
    guard outcome.ok else {
      saveErrorText = outcome.message
      return
    }
    var err = ""
    if Services.shared.applyScheduleEdit(outcome.segments, scope: scope, error: &err) {
      onSaved(scope == .todayOnly ? "时段已从今天的计划中删除。" : "时段已从模板中删除。")
    } else {
      saveErrorText = err
    }
  }
}

// MARK: - 根视图（NavigationSplitView + 原生液态玻璃工具栏）

struct SettingsRootView: View {
  @ObservedObject var viewModel: SettingsViewModel
  @ObservedObject private var services = Services.shared
  let onQuit: () -> Void

  var body: some View {
    let _ = services.uiRevision
    let pres = services.config.presentation
    let muted = services.config.notifications.muted
    let clearGlass = (pres.glassStyle == .clear)
    let section = viewModel.selectedSection ?? .schedule

    NavigationSplitView {
      VStack(spacing: 0) {
        List(SettingsSection.allCases, selection: $viewModel.selectedSection) { item in
          Label(item.title, systemImage: item.icon)
            .tag(item)
        }
        .listStyle(.sidebar)

        HStack {
          Text("ClassTime  ·  v\(appVersionString)")
            .font(.system(size: 11.5))
            .foregroundStyle(.secondary)
          Spacer()
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
      }
      .navigationSplitViewColumnWidth(min: 196, ideal: 216, max: 240)
    } detail: {
      ZStack {
        WindowTranslucentBackdrop(clearStyle: clearGlass)
          .ignoresSafeArea()

        if section == .display {
          // 显示与位置页：实时预览固定在上方，下方设置独立滚动。
          DisplaySettingsPage(viewModel: viewModel)
            .frame(maxWidth: 1040, maxHeight: .infinity, alignment: .topLeading)
            .padding(.horizontal, 26)
            .padding(.top, 18)
            .padding(.bottom, 20)
        } else {
          ScrollView {
            AdaptiveGlassGroup(spacing: 14) {
              Group {
                switch section {
                case .schedule:
                  ScheduleSettingsPage(viewModel: viewModel)
                case .display:
                  EmptyView()
                case .notifications:
                  NotificationsSettingsPage(viewModel: viewModel)
                case .general:
                  GeneralSettingsPage(onQuit: onQuit)
                }
              }
            }
            .frame(maxWidth: 1000, alignment: .leading)
            .padding(.horizontal, 26)
            .padding(.vertical, 22)
          }
          .scrollContentBackground(.hidden)
          .adaptiveScrollEdgeEffect(for: .top)
        }
      }
      .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
      .navigationTitle(section.title)
      .toolbar {
        ToolbarItemGroup(placement: .primaryAction) {
          Button {
            var next = pres
            next.visible.toggle()
            services.updatePresentation(next)
          } label: {
            Label(
              pres.visible ? "隐藏桌面组件" : "显示桌面组件",
              systemImage: pres.visible ? "rectangle.inset.filled" : "rectangle.dashed"
            )
          }
          .help(pres.visible ? "隐藏桌面悬浮组件" : "显示桌面悬浮组件")

          Button {
            services.setMuted(!muted)
          } label: {
            Label(
              muted ? "恢复提醒" : "暂停提醒",
              systemImage: muted ? "bell.slash.fill" : "bell"
            )
          }
          .help(muted ? "恢复边界系统通知" : "临时暂停边界通知（时间进度继续）")
        }
      }
    }
    .adaptiveClearWindowContainer()
  }
}

// MARK: - 通用液态玻璃卡片、徽标与对齐开关行

private struct SettingsCard<Content: View>: View {
  @ObservedObject private var services = Services.shared
  let content: Content

  init(@ViewBuilder content: () -> Content) {
    self.content = content()
  }

  var body: some View {
    let _ = services.uiRevision
    let clearGlass = (services.config.presentation.glassStyle == .clear)
    let radius: CGFloat = LiquidGlassCapabilities.isAvailable ? 16 : 12
    content
      .padding(16)
      .frame(maxWidth: .infinity, alignment: .leading)
      .adaptiveGlassCard(cornerRadius: radius, clearStyle: clearGlass)
  }
}

private struct StatusChip: View {
  @ObservedObject private var services = Services.shared
  let text: String
  let accent: Bool

  var body: some View {
    let _ = services.uiRevision
    let clearGlass = (services.config.presentation.glassStyle == .clear)
    Text(text)
      .font(.system(size: 12, weight: accent ? .medium : .regular))
      .monospacedDigit()
      .foregroundStyle(accent ? Color.accentColor : Color.secondary)
      .padding(.horizontal, 11)
      .padding(.vertical, 5)
      .adaptiveGlassCapsule(
        tint: accent ? .accentColor : nil,
        clearStyle: clearGlass,
        interactive: false
      )
  }
}

private struct AlignedToggleRow: View {
  let title: String
  let description: String
  let systemImage: String?
  @Binding var isOn: Bool
  var disabled: Bool = false

  init(
    title: String,
    description: String = "",
    systemImage: String? = nil,
    isOn: Binding<Bool>,
    disabled: Bool = false
  ) {
    self.title = title
    self.description = description
    self.systemImage = systemImage
    self._isOn = isOn
    self.disabled = disabled
  }

  var body: some View {
    HStack(spacing: 12) {
      if let systemImage {
        Image(systemName: systemImage)
          .font(.system(size: 17))
          .foregroundStyle(.secondary)
          .frame(width: 24, alignment: .center)
          .accessibilityHidden(true)
      }

      VStack(alignment: .leading, spacing: 2) {
        Text(title)
          .font(.system(size: 13.5, weight: .semibold))
          .foregroundStyle(disabled ? Color.secondary : Color.primary)
        if !description.isEmpty {
          Text(description)
            .font(.system(size: 12))
            .foregroundStyle(.secondary)
        }
      }

      Spacer(minLength: 12)

      Toggle("", isOn: $isOn)
        .toggleStyle(.switch)
        .labelsHidden()
        .disabled(disabled)
        .accessibilityLabel(title)
        .accessibilityHint(description)
    }
    .frame(minHeight: 34)
  }
}

// MARK: - 1. 作息计划页

private struct ScheduleSettingsPage: View {
  @ObservedObject var viewModel: SettingsViewModel
  @ObservedObject private var services = Services.shared

  var body: some View {
    let _ = services.uiRevision
    let cfg = services.config
    let now = nowClock()
    let snap = services.snapshot
    let todayHasOverride = hasOverride(cfg, dateKey: now.dateKey)
    let currentTemplate = selectedTemplate(cfg)
    let isBuiltInPreset = (cfg.selectedTemplateId == "high-school" || cfg.selectedTemplateId == "university")
    let displayedSegments: [Segment] = if viewModel.viewingTodayPlan {
      sortedByStart(services.currentEffectiveSegments)
    } else {
      sortedByStart(currentTemplate?.segments ?? [])
    }

    VStack(alignment: .leading, spacing: 14) {
      // 非阻断通知栏
      if !viewModel.noticeText.isEmpty {
        HStack(spacing: 10) {
          Image(
            systemName: viewModel.noticeIsError
              ? "exclamationmark.triangle.fill"
              : "checkmark.circle.fill"
          )
          .foregroundStyle(viewModel.noticeIsError ? Color.orange : Color.green)
          Text(viewModel.noticeText)
            .font(.system(size: 13))
          Spacer()
          Button {
            viewModel.noticeText = ""
          } label: {
            Image(systemName: "xmark")
              .font(.system(size: 11, weight: .semibold))
          }
          .buttonStyle(.plain)
          .accessibilityLabel("关闭提示")
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 9)
        .adaptiveGlassCard(
          cornerRadius: 12,
          clearStyle: cfg.presentation.glassStyle == .clear,
          tint: (viewModel.noticeIsError ? Color.orange : Color.green).opacity(0.16)
        )
      }

      // 页首：标题、状态徽标与显眼的“新增时段”主入口
      HStack(alignment: .top, spacing: 12) {
        VStack(alignment: .leading, spacing: 6) {
          Text("作息计划")
            .font(.system(size: 26, weight: .semibold))
          Text("管理一周的作息安排，包括上课、课间与午休。")
            .font(.system(size: 12.5))
            .foregroundStyle(.secondary)
          AdaptiveGlassGroup(spacing: 8) {
            HStack(spacing: 8) {
              StatusChip(text: headerNowChipText(snap: snap), accent: true)
              StatusChip(text: "今天 \(now.month)/\(now.day) \(Formatters.weekdayLabel(now.weekday))", accent: false)
            }
          }
        }
        Spacer()
        Button {
          viewModel.openEditor(segment: nil, defaultPermanent: !viewModel.viewingTodayPlan)
        } label: {
          Label("新增时段", systemImage: "plus")
        }
        .adaptiveGlassButton(prominent: true)
        .accessibilityLabel("新增时段")
      }

      // 模板与生效星期卡片
      SettingsCard {
        VStack(alignment: .leading, spacing: 14) {
          ViewThatFits(in: .horizontal) {
            HStack(spacing: 12) {
              templateSelectorRow(cfg: cfg)
              Spacer(minLength: 10)
              planScopePicker(todayHasOverride: todayHasOverride)
                .frame(width: 240)
            }
            VStack(alignment: .leading, spacing: 10) {
              templateSelectorRow(cfg: cfg)
              planScopePicker(todayHasOverride: todayHasOverride)
            }
          }

          Text(templateStatusLine(cfg: cfg, currentTemplate: currentTemplate, now: now, todayHasOverride: todayHasOverride))
            .font(.system(size: 12))
            .foregroundStyle(todayHasOverride ? Color.accentColor : Color.secondary)

          DisclosureGroup("修改模板名称", isExpanded: $viewModel.showTemplateRename) {
            VStack(alignment: .leading, spacing: 6) {
              Text("模板名称")
                .font(.system(size: 12.5, weight: .medium))
              TextField(
                "例如：我的高中课表",
                text: Binding(
                  get: { viewModel.templateNameDraft },
                  set: { viewModel.updateTemplateNameDraft($0) }
                )
              )
              .textFieldStyle(.roundedBorder)
              .onSubmit {
                viewModel.commitTemplateName()
              }
              .accessibilityLabel("模板名称")
              Text("修改名称后自动保存。")
                .font(.system(size: 11.5))
                .foregroundStyle(.secondary)
            }
            .padding(.top, 8)
          }

          VStack(alignment: .leading, spacing: 8) {
            Text("生效星期")
              .font(.system(size: 13.5, weight: .semibold))
            AdaptiveGlassGroup(spacing: 6) {
              HStack(spacing: 6) {
                ForEach(1...7, id: \.self) { day in
                  let active = currentTemplate?.isActive(on: day) ?? false
                  Button {
                    guard let tpl = currentTemplate else { return }
                    var next = tpl.activeWeekdays
                    if let idx = next.firstIndex(of: day) {
                      next.remove(at: idx)
                    } else {
                      next.append(day)
                    }
                    services.setActiveWeekdays(next)
                  } label: {
                    Text(Formatters.weekdayLabel(day))
                      .font(.system(size: 12.5, weight: active ? .semibold : .regular))
                      .frame(width: 48, height: 30)
                      .foregroundStyle(active ? Color.accentColor : Color.primary)
                      .adaptiveGlassCapsule(
                        tint: active ? .accentColor : nil,
                        clearStyle: cfg.presentation.glassStyle == .clear,
                        interactive: true
                      )
                  }
                  .buttonStyle(.plain)
                  .accessibilityLabel("\(Formatters.weekdayLabel(day))，\(active ? "已启用" : "未启用")")
                }
              }
            }
          }
        }
      }

      // 时段安排卡片（操作栏 + 分组时段列表 + 可展开时间轴 + 可展开课表预览）
      SettingsCard {
        VStack(alignment: .leading, spacing: 12) {
          HStack {
            Text("时段安排")
              .font(.system(size: 16, weight: .semibold))
            Spacer()
            AdaptiveGlassGroup(spacing: 8) {
              HStack(spacing: 8) {
                Button("恢复默认…") {
                  viewModel.showRestoreConfirm = true
                }
                .adaptiveGlassButton(prominent: false)
                .disabled(!isBuiltInPreset)
                .help(isBuiltInPreset ? "恢复当前内置预设的默认作息" : "仅内置高中和大学模板可恢复预设")

                Button("撤销今日调整") {
                  services.clearTodayOverride()
                  viewModel.noticeIsError = false
                  viewModel.noticeText = "已撤销今天的临时调整，恢复按常规模板执行。"
                }
                .adaptiveGlassButton(prominent: false)
                .disabled(!todayHasOverride)
                .help(todayHasOverride ? "撤销今天的临时覆盖，恢复按常规模板执行" : "今天没有临时调整可撤销")
              }
            }
          }

          if displayedSegments.isEmpty {
            VStack(alignment: .leading, spacing: 8) {
              Text(viewModel.viewingTodayPlan ? "今天没有安排的时段。" : "模板还没有安排时段。")
                .font(.system(size: 13))
                .foregroundStyle(.secondary)
              Button("添加时段") {
                viewModel.openEditor(segment: nil, defaultPermanent: !viewModel.viewingTodayPlan)
              }
              .adaptiveGlassButton(prominent: true)
            }
            .padding(.vertical, 8)
          } else {
            groupedSegmentList(displayedSegments)
          }

          Divider()

          DisclosureGroup("时间轴 · 拖动调整时段", isExpanded: $viewModel.showTimeline) {
            InteractiveTimelineView(
              model: viewModel.timelineModel,
              segments: displayedSegments,
              viewingTodayPlan: viewModel.viewingTodayPlan,
              snapMinutes: cfg.presentation.timelineSnapMinutes,
              onChangeSnap: { newSnap in
                var next = cfg.presentation
                next.timelineSnapMinutes = newSnap
                services.updatePresentation(next)
              },
              onCommitDrag: { segId, newStart, newEnd in
                viewModel.applyTimelineDragEdit(segmentId: segId, startMinute: newStart, endMinute: newEnd)
              },
              onEditSegment: { seg in
                viewModel.openEditor(segment: seg, defaultPermanent: !viewModel.viewingTodayPlan)
              },
              onCreateAtMinute: { minute in
                viewModel.openNewEditorAt(minute: minute, defaultPermanent: !viewModel.viewingTodayPlan)
              },
              onNoticeError: { message in
                viewModel.noticeIsError = true
                viewModel.noticeText = message
              }
            )
            .padding(.top, 8)
          }

          Divider()

          DisclosureGroup("课表预览", isExpanded: $viewModel.showTimetable) {
            WeeklyTimetableGridView(
              segments: displayedSegments,
              schedule: currentTemplate,
              permanent: !viewModel.viewingTodayPlan,
              todayDateKey: now.dateKey,
              onEditSegment: { seg in
                viewModel.openEditor(segment: seg, defaultPermanent: !viewModel.viewingTodayPlan)
              }
            )
            .padding(.top, 8)
          }
        }
      }

      // 可展开的今日概览卡片
      SettingsCard {
        DisclosureGroup("今日概览", isExpanded: $viewModel.showTodayOverview) {
          todayOverviewBody(cfg: cfg, now: now, snap: snap, todayHasOverride: todayHasOverride)
            .padding(.top, 10)
        }
      }
    }
    .onAppear {
      viewModel.syncDraftsFromConfig()
    }
    .sheet(item: $viewModel.activeEditor) { editor in
      SegmentEditorSheet(editor: editor)
    }
    .sheet(item: $viewModel.addTemplateSheet) { sheetModel in
      AddTemplateSheet(model: sheetModel)
    }
    .alert("恢复默认作息", isPresented: $viewModel.showRestoreConfirm) {
      Button("取消", role: .cancel) {}
      Button("恢复默认", role: .destructive) {
        var err = ""
        if services.restorePreset(error: &err) {
          viewModel.syncDraftsFromConfig()
          viewModel.noticeIsError = false
          viewModel.noticeText = "模板已恢复为随包预设。"
        } else {
          viewModel.noticeIsError = true
          viewModel.noticeText = "恢复失败：\(err)"
        }
      }
    } message: {
      Text("是否将当前模板恢复为随包预设？常规模板的自定义时段会被覆盖；今天的临时调整不会被清除。")
    }
  }

  @ViewBuilder
  private func templateSelectorRow(cfg: AppConfig) -> some View {
    HStack(spacing: 10) {
      Text("模板")
        .font(.system(size: 13.5, weight: .medium))
      Picker(
        "模板",
        selection: Binding(
          get: { cfg.selectedTemplateId },
          set: { newId in
            viewModel.commitTemplateName()
            viewModel.timelineModel.resetDrag()
            var err = ""
            _ = services.selectTemplate(newId, error: &err)
            viewModel.syncDraftsFromConfig()
          }
        )
      ) {
        ForEach(cfg.templates) { tpl in
          Text(tpl.name).tag(tpl.id)
        }
      }
      .labelsHidden()
      .frame(minWidth: 160, maxWidth: 260)

      Button("新增模板") {
        viewModel.openAddTemplate()
      }
      .adaptiveGlassButton(prominent: false)
    }
  }

  @ViewBuilder
  private func planScopePicker(todayHasOverride: Bool) -> some View {
    Picker(
      "查看的作息计划",
      selection: Binding(
        get: { viewModel.viewingTodayPlan },
        set: { next in
          viewModel.timelineModel.resetDrag()
          viewModel.viewingTodayPlan = next
        }
      )
    ) {
      Text("常规模板").tag(false)
      Text("今天的计划\(todayHasOverride ? " · 临时" : "")").tag(true)
    }
    .pickerStyle(.segmented)
    .labelsHidden()
  }

  private func templateStatusLine(
    cfg: AppConfig,
    currentTemplate: Schedule?,
    now: WallClock,
    todayHasOverride: Bool
  ) -> String {
    if todayHasOverride {
      return "今天使用临时调整，常规模板未被改写。"
    }
    guard let tpl = currentTemplate else {
      return "没有可用的模板。"
    }
    if !tpl.isActive(on: now.weekday) {
      return "今天不是该模板的生效日，桌面组件会显示“今天暂无安排”。"
    }
    return "今天按常规模板执行。修改后立即按新时段重新定位。"
  }

  private struct SegmentGroup: Identifiable {
    let id: String
    let title: String
    let tint: Color
    let items: [(segment: Segment, ordinal: Int)]
  }

  private func buildGroups(_ sorted: [Segment]) -> [SegmentGroup] {
    var groups: [SegmentGroup] = []
    var ordinal = 0
    for seg in sorted {
      if seg.kind == .focus { ordinal += 1 }
      let groupTitle: String = {
        if seg.kind == .lunch { return "午休" }
        if seg.start.hour < 12 { return "上午" }
        if seg.start.hour < 18 { return "下午" }
        return "晚上"
      }()
      let item = (segment: seg, ordinal: seg.kind == .focus ? ordinal : 0)
      if let last = groups.last, last.title == groupTitle {
        groups[groups.count - 1] = SegmentGroup(
          id: last.id,
          title: last.title,
          tint: last.tint,
          items: last.items + [item]
        )
      } else {
        groups.append(
          SegmentGroup(
            id: "\(groups.count)-\(groupTitle)",
            title: groupTitle,
            tint: WidgetPalette.kindColor(seg.kind),
            items: [item]
          )
        )
      }
    }
    return groups
  }

  @ViewBuilder
  private func groupedSegmentList(_ sorted: [Segment]) -> some View {
    let groups = buildGroups(sorted)
    VStack(alignment: .leading, spacing: 10) {
      ForEach(groups) { group in
        VStack(alignment: .leading, spacing: 5) {
          Text(group.title)
            .font(.system(size: 13.5, weight: .semibold))
            .foregroundStyle(group.tint)
            .padding(.leading, 2)
            .padding(.top, 2)

          ForEach(group.items, id: \.segment.id) { entry in
            SegmentRowView(
              segment: entry.segment,
              focusOrdinal: entry.ordinal,
              onEdit: {
                viewModel.openEditor(segment: entry.segment, defaultPermanent: !viewModel.viewingTodayPlan)
              },
              onDuplicate: {
                viewModel.openDuplicateEditor(segment: entry.segment, defaultPermanent: !viewModel.viewingTodayPlan)
              }
            )
          }
        }
      }
    }
  }

  @ViewBuilder
  private func todayOverviewBody(
    cfg: AppConfig,
    now: WallClock,
    snap: Snapshot,
    todayHasOverride: Bool
  ) -> some View {
    let segments = sortedByStart(services.currentEffectiveSegments)
    VStack(alignment: .leading, spacing: 8) {
      if segments.isEmpty {
        if todayHasOverride {
          Text("今天的计划为空。").font(.system(size: 12.5)).foregroundStyle(.secondary)
        } else if let tpl = selectedTemplate(cfg), !tpl.isActive(on: now.weekday) {
          Text("今天未启用此模板（休息日）。").font(.system(size: 12.5)).foregroundStyle(.secondary)
        } else {
          Text("今天没有安排的时段。").font(.system(size: 12.5)).foregroundStyle(.secondary)
        }
      } else {
        if let cur = snap.current {
          let total = cur.durationMinutes
          let elapsed = Int((Double(total) * snap.progress).rounded())
          let name = (cur.kind == .focus && cur.focusOrdinal > 0)
            ? "第 \(cur.focusOrdinal) 节 · \(cur.label)"
            : cur.label
          Text("当前：\(name)")
            .font(.system(size: 13.5, weight: .semibold))
          Text("进行 \(Formatters.minutes(elapsed)) / \(Formatters.minutes(total))（\(Formatters.percent(snap.progress))），结束于 \(Formatters.clock(cur.end))")
            .font(.system(size: 12))
            .monospacedDigit()
            .foregroundStyle(.secondary)
        } else if let nxt = snap.next {
          Text("未在时段内 · 下一个 \(nxt.label) \(Formatters.clock(nxt.start))–\(Formatters.clock(nxt.end))")
            .font(.system(size: 13.5, weight: .medium))
        } else {
          Text("今天到此结束。")
            .font(.system(size: 13.5, weight: .medium))
        }

        Divider()

        Text("今天的时段列表")
          .font(.system(size: 12))
          .foregroundStyle(.secondary)

        VStack(spacing: 4) {
          ForEach(segments) { seg in
            let isCurrent = (snap.current?.id == seg.id)
            let color = WidgetPalette.kindColor(seg.kind)
            HStack(spacing: 8) {
              Text(Formatters.clock(seg.start))
                .font(.system(size: 12, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 48, alignment: .leading)
              Circle()
                .fill(color)
                .frame(width: 8, height: 8)
              Text(seg.label)
                .font(.system(size: 12.5, weight: isCurrent ? .semibold : .regular))
                .lineLimit(1)
              Spacer()
              Text(Formatters.range(seg))
                .font(.system(size: 11.5, design: .monospaced))
                .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(
              RoundedRectangle(cornerRadius: 6, style: .continuous)
                .fill(isCurrent ? color.opacity(0.16) : Color.clear)
            )
          }
        }
      }
    }
  }

  private func headerNowChipText(snap: Snapshot) -> String {
    if let cur = snap.current {
      let title = (cur.kind == .focus && cur.focusOrdinal > 0)
        ? "第 \(cur.focusOrdinal) 节"
        : Formatters.segmentKindLabel(cur.kind)
      return "\(title) \(Formatters.clock(cur.start))–\(Formatters.clock(cur.end))"
    }
    if let nxt = snap.next {
      return "下一节 \(Formatters.clock(nxt.start))"
    }
    return snap.hasSchedule ? "今日已结束" : "空闲"
  }
}

private struct SegmentRowView: View {
  let segment: Segment
  let focusOrdinal: Int
  let onEdit: () -> Void
  let onDuplicate: () -> Void

  var body: some View {
    let kindColor = WidgetPalette.kindColor(segment.kind)
    let badgeText = (segment.kind == .focus && focusOrdinal > 0)
      ? "第 \(focusOrdinal) 节"
      : Formatters.segmentKindLabel(segment.kind)

    HStack(spacing: 12) {
      Text(badgeText)
        .font(.system(size: 11, weight: .semibold))
        .foregroundStyle(kindColor)
        .padding(.vertical, 3)
        .frame(width: 54, alignment: .center)
        .background(
          Capsule()
            .fill(kindColor.opacity(0.16))
        )

      VStack(alignment: .leading, spacing: 2) {
        Text(segment.label)
          .font(.system(size: 13.5, weight: .semibold))
        Text("\(Formatters.range(segment)) · \(Formatters.minutes(segment.durationMinutes))")
          .font(.system(size: 12))
          .monospacedDigit()
          .foregroundStyle(.secondary)
      }

      Spacer()

      HStack(spacing: 6) {
        Button("编辑", action: onEdit)
          .controlSize(.small)
          .accessibilityLabel("编辑 \(segment.label)")
        Button("复制", action: onDuplicate)
          .controlSize(.small)
          .accessibilityLabel("复制 \(segment.label)")
      }
    }
    .padding(.horizontal, 12)
    .padding(.vertical, 8)
    .background(
      RoundedRectangle(cornerRadius: 9, style: .continuous)
        .fill(kindColor.opacity(0.07))
        .overlay(
          RoundedRectangle(cornerRadius: 9, style: .continuous)
            .stroke(kindColor.opacity(0.20), lineWidth: 1)
        )
    )
    .contentShape(Rectangle())
    .onTapGesture(perform: onEdit)
  }
}

// MARK: - 新增模板 Sheet

private struct AddTemplateSheet: View {
  @ObservedObject var model: AddTemplateViewModel

  var body: some View {
    VStack(alignment: .leading, spacing: 14) {
      Text("新增模板")
        .font(.system(size: 19, weight: .semibold))

      VStack(alignment: .leading, spacing: 5) {
        Text("模板名称")
          .font(.system(size: 12.5, weight: .medium))
        TextField("新模板", text: $model.name)
          .textFieldStyle(.roundedBorder)
          .accessibilityLabel("新模板名称")
      }

      VStack(alignment: .leading, spacing: 6) {
        Text("创建方式")
          .font(.system(size: 12.5, weight: .medium))
        Picker("模板创建方式", selection: $model.copyCurrent) {
          Text("复制当前模板的时段和生效星期").tag(true)
          Text("空白模板（默认周一至周五生效）").tag(false)
        }
        .pickerStyle(.radioGroup)
        .labelsHidden()
      }

      Text("创建后立即切换到新模板；若今天有临时调整，今天仍按临时计划执行。")
        .font(.system(size: 12))
        .foregroundStyle(.secondary)

      if !model.errorText.isEmpty {
        Text(model.errorText)
          .font(.system(size: 12.5))
          .foregroundStyle(.red)
      }

      HStack {
        Spacer()
        Button("取消") {
          model.onCancel()
        }
        .adaptiveGlassButton(prominent: false)
        .keyboardShortcut(.cancelAction)

        Button("创建并使用") {
          model.submit()
        }
        .adaptiveGlassButton(prominent: true)
        .keyboardShortcut(.defaultAction)
      }
    }
    .padding(20)
    .frame(width: 420)
    .adaptiveTranslucentSheet()
  }
}

// MARK: - 时段编辑工作台（1 分钟数字框 + 5 分钟滑块双向同步 + 上下文变更/冲突预览）

private struct SegmentEditorSheet: View {
  @ObservedObject var editor: SegmentEditorViewModel
  private let services = Services.shared

  var body: some View {
    let evaluation = editor.draftEvaluation
    let todayHasOverride = hasOverride(services.config, dateKey: services.todayKey)

    VStack(alignment: .leading, spacing: 14) {
      HStack {
        Text(editor.titleText)
          .font(.system(size: 20, weight: .semibold))
        Spacer()
        Picker("保存作用范围", selection: $editor.scope) {
          Text("仅今天").tag(SaveScope.todayOnly)
          Text("永久修改模板").tag(SaveScope.permanent)
        }
        .pickerStyle(.segmented)
        .frame(width: 240)
      }

      if editor.scope == .permanent && todayHasOverride {
        Label(
          "今天仍按临时方案执行；永久修改只影响之后的常规模板。",
          systemImage: "info.circle.fill"
        )
        .font(.system(size: 12))
        .foregroundStyle(Color.accentColor)
      } else {
        Text(
          editor.scope == .todayOnly
            ? "只影响今天（\(services.todayKey)），不改动常规模板。"
            : "修改会写入当前模板，并按生效星期重复。"
        )
        .font(.system(size: 12))
        .foregroundStyle(.secondary)
      }

      Divider()

      HStack(alignment: .top, spacing: 18) {
        // 左侧表单
        VStack(alignment: .leading, spacing: 12) {
          HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
              Text("名称")
                .font(.system(size: 12.5, weight: .medium))
              TextField("例如：第 3 节 / 午休", text: $editor.label)
                .textFieldStyle(.roundedBorder)
            }

            VStack(alignment: .leading, spacing: 4) {
              Text("类型")
                .font(.system(size: 12.5, weight: .medium))
              Picker(
                "类型",
                selection: Binding(
                  get: { editor.kind },
                  set: { editor.updateKind($0) }
                )
              ) {
                Text("上课").tag(SegmentKind.focus)
                Text("课间").tag(SegmentKind.shortBreak)
                Text("长休息").tag(SegmentKind.longBreak)
                Text("午休").tag(SegmentKind.lunch)
              }
              .labelsHidden()
              .frame(width: 115)
            }
          }

          HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
              Text("开始（HH:mm）")
                .font(.system(size: 12.5, weight: .medium))
              TextField(
                "10:00",
                text: Binding(
                  get: { editor.startText },
                  set: { editor.updateStartText($0) }
                )
              )
              .textFieldStyle(.roundedBorder)
              .font(.system(size: 13, design: .monospaced))
            }

            VStack(alignment: .leading, spacing: 4) {
              Text("结束（HH:mm）")
                .font(.system(size: 12.5, weight: .medium))
              TextField(
                "10:45",
                text: Binding(
                  get: { editor.endText },
                  set: { editor.updateEndText($0) }
                )
              )
              .textFieldStyle(.roundedBorder)
              .font(.system(size: 13, design: .monospaced))
            }
          }

          VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
              Text("时长（分钟）")
                .font(.system(size: 12.5, weight: .medium))
              TextField(
                "分钟",
                text: Binding(
                  get: { editor.durationText },
                  set: { editor.updateMinutesText($0) }
                )
              )
              .textFieldStyle(.roundedBorder)
              .font(.system(size: 13, design: .monospaced))
              .frame(width: 78)
              Text("精确到 1 分钟")
                .font(.system(size: 12))
                .foregroundStyle(.secondary)
            }

            Slider(
              value: Binding(
                get: { editor.sliderValue },
                set: { editor.updateFromSlider($0) }
              ),
              in: editor.sliderRange,
              step: 5
            )
            .accessibilityLabel("调整时长（每格 5 分钟）")

            Text(editor.sliderHintText)
              .font(.system(size: 11.5))
              .foregroundStyle(.secondary)
          }

          if !editor.isNewSegment {
            VStack(alignment: .leading, spacing: 6) {
              Text("后续时段")
                .font(.system(size: 12.5, weight: .medium))
              Picker("后续时段", selection: $editor.strategy) {
                Text("顺延后续时段").tag(ShiftStrategy.shift)
                Text("保持原时间").tag(ShiftStrategy.keep)
              }
              .pickerStyle(.radioGroup)
              .labelsHidden()
            }
          }

          if !evaluation.ok {
            Label(editor.conflictDescription(evaluation), systemImage: "xmark.octagon.fill")
              .font(.system(size: 12.5, weight: .medium))
              .foregroundStyle(.red)
              .padding(10)
              .frame(maxWidth: .infinity, alignment: .leading)
              .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                  .fill(Color.red.opacity(0.10))
              )
          } else if !editor.saveErrorText.isEmpty {
            Text(editor.saveErrorText)
              .font(.system(size: 12.5))
              .foregroundStyle(.red)
          }
        }
        .frame(maxWidth: .infinity, alignment: .leading)

        // 右侧上下文草稿实时预览（保存前不修改真实日程与通知）
        editorContextualPreview(evaluation: evaluation)
          .padding(12)
          .frame(width: 276, alignment: .topLeading)
          .adaptiveGlassCard(
            cornerRadius: 12,
            clearStyle: services.config.presentation.glassStyle == .clear
          )
      }

      Divider()

      HStack {
        if editor.existsInSelectedScope {
          Button(role: .destructive) {
            editor.showDeleteConfirm = true
          } label: {
            Label("删除时段", systemImage: "trash")
          }
          .adaptiveGlassButton(prominent: false)
        }
        Spacer()
        Button("取消") {
          editor.onCancel()
        }
        .adaptiveGlassButton(prominent: false)
        .keyboardShortcut(.cancelAction)

        Button("保存调整") {
          editor.commitSave()
        }
        .adaptiveGlassButton(prominent: true)
        .disabled(!evaluation.ok)
        .keyboardShortcut(.defaultAction)
      }
    }
    .padding(22)
    .frame(width: 720)
    .adaptiveTranslucentSheet()
    .alert("删除时段", isPresented: $editor.showDeleteConfirm) {
      Button("取消", role: .cancel) {}
      Button("删除", role: .destructive) {
        editor.performDelete()
      }
    } message: {
      let name = editor.label.isEmpty ? "未命名时段" : editor.label
      let target = (editor.scope == .permanent) ? "常规模板" : "今天的计划"
      Text("删除“\(name)”？此操作会写入\(target)，且无法撤销。")
    }
  }

  @ViewBuilder
  private func editorContextualPreview(evaluation: EditOutcome) -> some View {
    let base = editor.baseSegments
    let scopeLabel = (editor.scope == .permanent) ? "常规模板" : "今天的计划"

    VStack(alignment: .leading, spacing: 8) {
      Text("课表预览")
        .font(.system(size: 15, weight: .semibold))
      Text("\(scopeLabel) · 尚未保存")
        .font(.system(size: 11.5))
        .foregroundStyle(.secondary)

      if let draft = editor.draftSegment {
        let previewSegments: [Segment] = {
          if !evaluation.segments.isEmpty {
            return sortedByStart(evaluation.segments)
          }
          var merged = base
          if let idx = merged.firstIndex(where: { $0.id == editor.segmentId }) {
            merged[idx] = draft
          } else {
            merged.append(draft)
          }
          return sortedByStart(merged)
        }()

        if let editedIdx = previewSegments.firstIndex(where: { $0.id == editor.segmentId }) {
          let firstIdx = max(0, editedIdx - 1)
          let lastIdx = min(previewSegments.count, editedIdx + 4)
          let window = Array(previewSegments[firstIdx..<lastIdx])
          let rangeStart = window.first?.start.totalMinutes ?? 0
          let rangeEnd = max(rangeStart + 1, window.map(\.end.totalMinutes).max() ?? (rangeStart + 1))

          ScrollView {
            VStack(alignment: .leading, spacing: 9) {
              ForEach(window) { seg in
                let original = base.first(where: { $0.id == seg.id })
                let changed = original.map { $0.start != seg.start || $0.end != seg.end } ?? false
                let overlap = (evaluation.status == .overlap) && previewSegments.contains { other in
                  seg.id != other.id && seg.start < other.end && other.start < seg.end
                }
                let conflict = overlap || evaluation.conflictIds.contains(seg.id)
                let isDraft = (seg.id == editor.segmentId)

                let caption: String = {
                  var s = isDraft ? "草稿 · \(seg.label)" : seg.label
                  if conflict {
                    s += " · 冲突"
                  } else if !isDraft && changed {
                    s += " · 已顺延"
                  }
                  return s
                }()

                let rangeLine: String = if let orig = original, changed {
                  "\(Formatters.range(orig))  →  \(Formatters.range(seg))"
                } else {
                  Formatters.range(seg)
                }

                VStack(alignment: .leading, spacing: 3) {
                  Text(caption)
                    .font(.system(size: 12.5, weight: isDraft ? .semibold : .medium))
                    .foregroundStyle(conflict ? Color.red : Color.primary)
                    .lineLimit(1)
                  Text(rangeLine)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(conflict ? Color.red : Color.secondary)

                  GeometryReader { geo in
                    let totalSpan = Double(max(1, rangeEnd - rangeStart))
                    let leftRatio = Double(seg.start.totalMinutes - rangeStart) / totalSpan
                    let widthRatio = Double(max(1, seg.durationMinutes)) / totalSpan
                    ZStack(alignment: .leading) {
                      Capsule().fill(Color.primary.opacity(0.12))
                      Capsule()
                        .fill(conflict ? Color.red : WidgetPalette.kindColor(seg.kind))
                        .frame(width: max(4, geo.size.width * CGFloat(widthRatio)))
                        .offset(x: geo.size.width * CGFloat(leftRatio))
                    }
                  }
                  .frame(height: 6)
                }
              }
            }
          }
          .frame(maxHeight: 280)
        }
      } else {
        Text("请输入有效的起止时间和整分钟时长。")
          .font(.system(size: 12))
          .foregroundStyle(.secondary)
      }
    }
  }
}

// MARK: - 2. 显示与位置页（顶部固定实时预览 + 下方独立滚动设置）

private struct DisplaySettingsPage: View {
  @ObservedObject var viewModel: SettingsViewModel
  @ObservedObject private var services = Services.shared

  var body: some View {
    let _ = services.uiRevision
    let savedPres = services.config.presentation
    let previewPres = viewModel.effectivePreviewPresentation
    let clearGlass = (savedPres.glassStyle == .clear)
    let now = viewModel.previewNow
    let plan = services.plan(for: now)

    VStack(alignment: .leading, spacing: 12) {
      VStack(alignment: .leading, spacing: 3) {
        Text("显示与位置")
          .font(.system(size: 26, weight: .semibold))
        Text("调整桌面组件的显示样式、显示范围与位置。")
          .font(.system(size: 12.5))
          .foregroundStyle(.secondary)
      }

      // 固定在顶部的实时预览卡（滚动下方选项时始终可见）
      DisplayLivePreviewCard(
        presentation: previewPres,
        segments: plan.segments,
        now: now,
        hovered: viewModel.previewHovered,
        clearGlass: clearGlass,
        onHoverChange: { inside in
          viewModel.previewHovered = inside
        }
      )

      // 下方独立滚动的设置卡片
      ScrollView {
        AdaptiveGlassGroup(spacing: 12) {
          VStack(alignment: .leading, spacing: 12) {
            // 显示形式卡片
            SettingsCard {
              VStack(alignment: .leading, spacing: 10) {
                Text("显示形式")
                  .font(.system(size: 15.5, weight: .semibold))
                Text("选择桌面组件的样式。")
                  .font(.system(size: 12))
                  .foregroundStyle(.secondary)
                ViewModeChoiceGrid(
                  selected: savedPres.view,
                  clearGlass: clearGlass,
                  onSelect: { newView in
                    guard savedPres.view != newView else { return }
                    var next = savedPres
                    next.view = newView
                    services.updatePresentation(next)
                  }
                )
              }
            }

            // 显示设置 + 位置设置（宽窗口双列，窄窗口纵向堆叠）
            ViewThatFits(in: .horizontal) {
              HStack(alignment: .top, spacing: 12) {
                displayOptionsCard(savedPres: savedPres, previewPres: previewPres)
                locationOptionsCard(savedPres: savedPres, previewPres: previewPres)
                  .frame(width: 390)
              }
              VStack(alignment: .leading, spacing: 12) {
                displayOptionsCard(savedPres: savedPres, previewPres: previewPres)
                locationOptionsCard(savedPres: savedPres, previewPres: previewPres)
              }
            }

            // 行为设置卡片
            SettingsCard {
              VStack(alignment: .leading, spacing: 10) {
                Text("行为设置")
                  .font(.system(size: 15.5, weight: .semibold))

                AlignedToggleRow(
                  title: "显示桌面组件",
                  isOn: Binding(
                    get: { savedPres.visible },
                    set: { val in
                      var next = savedPres
                      next.visible = val
                      services.updatePresentation(next)
                    }
                  )
                )

                if savedPres.view != .screenEdge {
                  Divider()
                  AlignedToggleRow(
                    title: "紧贴边缘",
                    description: "关闭后保留一点间距",
                    isOn: Binding(
                      get: { savedPres.flushWithEdge },
                      set: { val in
                        var next = savedPres
                        next.flushWithEdge = val
                        services.updatePresentation(next)
                      }
                    )
                  )

                  Divider()
                  AlignedToggleRow(
                    title: "贴边自动隐藏",
                    description: "鼠标靠近时展开组件",
                    isOn: Binding(
                      get: { savedPres.autoHideAtEdge },
                      set: { val in
                        var next = savedPres
                        next.autoHideAtEdge = val
                        services.updatePresentation(next)
                      }
                    )
                  )
                }
              }
            }
          }
          .padding(.vertical, 2)
        }
      }
      .scrollContentBackground(.hidden)
      .adaptiveScrollEdgeEffect(for: .top)
    }
    .onAppear {
      viewModel.refreshDisplays()
    }
  }

  @ViewBuilder
  private func displayOptionsCard(
    savedPres: PresentationSettings,
    previewPres: PresentationSettings
  ) -> some View {
    SettingsCard {
      VStack(alignment: .leading, spacing: 12) {
        Text("显示设置")
          .font(.system(size: 15.5, weight: .semibold))

        VStack(alignment: .leading, spacing: 6) {
          Text("显示范围")
            .font(.system(size: 13.5, weight: .medium))
          Picker(
            "显示范围",
            selection: Binding(
              get: { savedPres.scope },
              set: { s in
                var next = savedPres
                next.scope = s
                services.updatePresentation(next)
              }
            )
          ) {
            Text("全天").tag(PresentationSettings.ScopeMode.allDay)
            Text("当前时段").tag(PresentationSettings.ScopeMode.currentBlock)
          }
          .pickerStyle(.segmented)
          .labelsHidden()
        }

        if savedPres.view == .bar {
          Divider()

          VStack(alignment: .leading, spacing: 6) {
            let currentLength = viewModel.pendingBarLength ?? savedPres.effectiveBarLength
            let isDefaultLength = (viewModel.pendingBarLength == nil && savedPres.barLengthDip == PresentationSettings.defaultBarLength)
            HStack {
              Text("长条长度")
                .font(.system(size: 13.5, weight: .medium))
              Spacer()
              Text("\(isDefaultLength ? "默认 · " : "")\(currentLength) pt")
                .font(.system(size: 12.5, design: .monospaced))
                .foregroundStyle(.secondary)
            }
            Slider(
              value: Binding(
                get: { Double(currentLength) },
                set: { val in
                  viewModel.previewBarLength(Int(val.rounded()))
                }
              ),
              in: Double(PresentationSettings.minBarLength)...Double(PresentationSettings.maxBarLength),
              step: 10
            )
            .accessibilityLabel("长条长度")

            HStack {
              Text("左右贴边时调整竖向长度。")
                .font(.system(size: 11.5))
                .foregroundStyle(.secondary)
              Spacer()
              Button("恢复默认长度") {
                viewModel.resetBarLength()
              }
              .controlSize(.small)
            }
          }

          Divider()

          VStack(alignment: .leading, spacing: 8) {
            let activeHex = viewModel.pendingBarBackground ?? savedPres.barBackground
            let hasCustomBg = !activeHex.isEmpty

            AlignedToggleRow(
              title: "自定义背景色",
              description: LiquidGlassCapabilities.isAvailable
                ? "关闭时使用原生液态玻璃背景"
                : "关闭时使用系统背景材质",
              isOn: Binding(
                get: { hasCustomBg },
                set: { enabled in
                  viewModel.setCustomBarBackgroundEnabled(enabled)
                }
              )
            )

            if hasCustomBg {
              HStack {
                Text("长条背景颜色")
                  .font(.system(size: 13))
                  .foregroundStyle(.secondary)
                Spacer()
                ColorPicker(
                  "长条背景颜色",
                  selection: Binding(
                    get: {
                      WidgetPalette.color(fromHex: activeHex) ?? Color(nsColor: .windowBackgroundColor)
                    },
                    set: { newColor in
                      let hex = WidgetPalette.hexString(from: newColor)
                      viewModel.previewBarBackground(hex)
                    }
                  ),
                  supportsOpacity: false
                )
                .labelsHidden()
                .accessibilityLabel("长条背景颜色")

                Text("#\(activeHex.uppercased())")
                  .font(.system(size: 12, design: .monospaced))
                  .foregroundStyle(.secondary)
              }
            }
          }
        }

        Divider()

        VStack(alignment: .leading, spacing: 6) {
          Text("顶部状态栏显示")
            .font(.system(size: 13.5, weight: .medium))
          Picker(
            "顶部状态栏显示",
            selection: Binding(
              get: { savedPres.menuBarDisplay },
              set: { mode in
                var next = savedPres
                next.menuBarDisplay = mode
                services.updatePresentation(next)
              }
            )
          ) {
            Text("仅图标").tag(PresentationSettings.MenuBarDisplayMode.iconOnly)
            Text("倒计时").tag(PresentationSettings.MenuBarDisplayMode.countdown)
            Text("节次与倒计时").tag(PresentationSettings.MenuBarDisplayMode.detailed)
          }
          .pickerStyle(.segmented)
          .labelsHidden()
        }

        if LiquidGlassCapabilities.isAvailable {
          Divider()
          VStack(alignment: .leading, spacing: 6) {
            Text("液态玻璃通透度")
              .font(.system(size: 13.5, weight: .medium))
            Picker(
              "液态玻璃通透度",
              selection: Binding(
                get: { savedPres.glassStyle },
                set: { style in
                  var next = savedPres
                  next.glassStyle = style
                  services.updatePresentation(next)
                }
              )
            ) {
              Text("通透玻璃（推荐）").tag(PresentationSettings.GlassStyle.clear)
              Text("标准玻璃").tag(PresentationSettings.GlassStyle.regular)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            Text("开启系统「减少透明度 / 增强对比度」时自动保证可读性。")
              .font(.system(size: 11.5))
              .foregroundStyle(.secondary)
          }
        }
      }
    }
  }

  @ViewBuilder
  private func locationOptionsCard(
    savedPres: PresentationSettings,
    previewPres: PresentationSettings
  ) -> some View {
    let displays = viewModel.displays
    let savedMonitorId = savedPres.monitorId
    let monitorAvailable = savedMonitorId.isEmpty || displays.contains(where: { $0.id == savedMonitorId })

    SettingsCard {
      VStack(alignment: .leading, spacing: 12) {
        Text("位置设置")
          .font(.system(size: 15.5, weight: .semibold))

        // 多显示器选择
        VStack(alignment: .leading, spacing: 5) {
          Text("组件所在显示器")
            .font(.system(size: 13.5, weight: .medium))
          Picker(
            "组件所在显示器",
            selection: Binding(
              get: { savedMonitorId },
              set: { newId in
                guard savedPres.monitorId != newId else { return }
                var next = savedPres
                next.monitorId = newId
                services.updatePresentation(next)
              }
            )
          ) {
            Text("跟随系统主显示器").tag("")
            ForEach(displays) { display in
              Text(display.label).tag(display.id)
            }
            if !monitorAvailable {
              Text("上次选择的显示器不可用").tag(savedMonitorId)
            }
          }
          .labelsHidden()
          .accessibilityLabel("组件所在显示器")

          Text(
            monitorAvailable
              ? "只改变组件所在位置，不更改 macOS 的主显示器。"
              : "该显示器不可用，组件定位时会回退到系统主屏。"
          )
          .font(.system(size: 11.5))
          .foregroundStyle(monitorAvailable ? Color.secondary : Color.orange)
        }

        Divider()

        if savedPres.view == .screenEdge {
          let currentThickness = viewModel.pendingScreenEdgeThickness ?? savedPres.screenEdgeThickness
          VStack(alignment: .leading, spacing: 6) {
            HStack {
              Text("边框粗细")
                .font(.system(size: 13.5, weight: .medium))
              Spacer()
              Text("\(currentThickness) pt")
                .font(.system(size: 12.5, design: .monospaced))
                .foregroundStyle(.secondary)
            }
            Slider(
              value: Binding(
                get: { Double(currentThickness) },
                set: { val in
                  viewModel.previewScreenEdgeThickness(Int(val.rounded()))
                }
              ),
              in: Double(PresentationSettings.minScreenEdgeThickness)...Double(PresentationSettings.maxScreenEdgeThickness),
              step: 1
            )
            .accessibilityLabel("边框粗细")
            Text("鼠标可穿过边框，全天进度从首段开始到末段结束。")
              .font(.system(size: 11.5))
              .foregroundStyle(.secondary)
          }

          if savedPres.scope == .allDay {
            Divider()
            VStack(alignment: .leading, spacing: 6) {
              Text("全天配色")
                .font(.system(size: 13.5, weight: .medium))
              Picker(
                "全天配色",
                selection: Binding(
                  get: { savedPres.screenEdgeColors },
                  set: { colors in
                    guard savedPres.screenEdgeColors != colors else { return }
                    var next = savedPres
                    next.screenEdgeColors = colors
                    services.updatePresentation(next)
                  }
                )
              ) {
                Text("按时段分色").tag(PresentationSettings.ScreenEdgeColors.schedule)
                Text("跟随当前时段").tag(PresentationSettings.ScreenEdgeColors.currentState)
              }
              .pickerStyle(.segmented)
              .labelsHidden()
              Text("分色按各时段的实际时长排列，较浅部分尚未到达。")
                .font(.system(size: 11.5))
                .foregroundStyle(.secondary)
            }
          }
        } else {
          VStack(alignment: .leading, spacing: 10) {
            Text("贴边位置")
              .font(.system(size: 13.5, weight: .medium))
            Picker(
              "贴边位置",
              selection: Binding(
                get: { savedPres.edge },
                set: { newEdge in
                  var next = savedPres
                  next.edge = newEdge
                  next.offsetX = 0
                  next.offsetY = 0
                  services.updatePresentation(next)
                }
              )
            ) {
              Text("顶部").tag(PresentationSettings.Edge.top)
              Text("底部").tag(PresentationSettings.Edge.bottom)
              Text("左侧").tag(PresentationSettings.Edge.left)
              Text("右侧").tag(PresentationSettings.Edge.right)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            HStack(spacing: 8) {
              Button("恢复居中位置") {
                var next = savedPres
                next.visible = true
                next.offsetX = 0
                next.offsetY = 0
                services.updatePresentation(next)
              }
              .adaptiveGlassButton(prominent: false)

              Text("也可连续三击桌面组件归中。")
                .font(.system(size: 11.5))
                .foregroundStyle(.secondary)
            }
          }
        }
      }
    }
  }
}

// MARK: - 3. 通知页

private struct NotificationsSettingsPage: View {
  @ObservedObject var viewModel: SettingsViewModel
  @ObservedObject private var services = Services.shared

  var body: some View {
    let _ = services.uiRevision
    let notif = services.config.notifications

    VStack(alignment: .leading, spacing: 14) {
      Text("通知")
        .font(.system(size: 26, weight: .semibold))

      // 居中 Hero 摘要
      VStack(spacing: 6) {
        ZStack {
          RoundedRectangle(cornerRadius: 16, style: .continuous)
            .fill(Color.accentColor.opacity(0.14))
            .frame(width: 60, height: 60)
          Image(systemName: "bell.badge.fill")
            .font(.system(size: 26))
            .foregroundStyle(Color.accentColor)
        }
        Text("作息提醒")
          .font(.system(size: 20, weight: .semibold))
        Text("在合适的时间开始工作与休息。")
          .font(.system(size: 12.5))
          .foregroundStyle(.secondary)
        let status = effectiveNotificationState(notif)
        Text(status.text)
          .font(.system(size: 13, weight: .medium))
          .foregroundStyle(status.color)
          .multilineTextAlignment(.center)
      }
      .frame(maxWidth: .infinity)
      .padding(.vertical, 6)

      // 总开关与暂停卡
      SettingsCard {
        VStack(alignment: .leading, spacing: 10) {
          AlignedToggleRow(
            title: "启用作息提醒",
            description: "根据作息计划在关键时间点即时发送 macOS 系统通知（不预排未来队列）。",
            systemImage: "bell.fill",
            isOn: Binding(
              get: { notif.enabled },
              set: { val in
                var next = notif
                next.enabled = val
                services.updateNotifications(next)
              }
            )
          )
          Divider()
          AlignedToggleRow(
            title: "临时暂停提醒",
            description: "暂停期间不再发送通知，但作息进度会继续运行；恢复后不补发旧提醒。",
            systemImage: "bell.slash.fill",
            isOn: Binding(
              get: { notif.muted },
              set: { val in
                services.setMuted(val)
              }
            )
          )
        }
      }

      Text("提醒时机")
        .font(.system(size: 15.5, weight: .semibold))
        .padding(.top, 2)

      SettingsCard {
        VStack(alignment: .leading, spacing: 8) {
          AlignedToggleRow(
            title: "上课开始与结束",
            systemImage: "book.fill",
            isOn: Binding(
              get: { notif.focusBoundaries },
              set: { val in
                var next = notif
                next.focusBoundaries = val
                services.updateNotifications(next)
              }
            ),
            disabled: !notif.enabled
          )
          Divider()
          AlignedToggleRow(
            title: "课间开始与结束",
            systemImage: "cup.and.saucer.fill",
            isOn: Binding(
              get: { notif.restBoundaries },
              set: { val in
                var next = notif
                next.restBoundaries = val
                services.updateNotifications(next)
              }
            ),
            disabled: !notif.enabled
          )
          Divider()
          AlignedToggleRow(
            title: "午休开始与结束",
            systemImage: "moon.zzz.fill",
            isOn: Binding(
              get: { notif.lunchBoundaries },
              set: { val in
                var next = notif
                next.lunchBoundaries = val
                services.updateNotifications(next)
              }
            ),
            disabled: !notif.enabled
          )
        }
      }

      Text("系统通知")
        .font(.system(size: 15.5, weight: .semibold))
        .padding(.top, 2)

      SettingsCard {
        VStack(alignment: .leading, spacing: 10) {
          Text("macOS 通知权限")
            .font(.system(size: 15, weight: .semibold))
          Text(viewModel.permissionText)
            .font(.system(size: 12.5))
            .foregroundStyle(.secondary)

          Divider()

          AdaptiveGlassGroup(spacing: 8) {
            HStack(spacing: 10) {
              Button("发送测试通知") {
                NotificationService.shared.sendTestNotification { _, detail in
                  viewModel.syncPermissionState()
                  viewModel.testFeedback = detail
                }
              }
              .adaptiveGlassButton(prominent: true)

              Button("打开系统通知设置") {
                NotificationService.shared.openSystemNotificationSettings()
              }
              .adaptiveGlassButton(prominent: false)
            }
          }

          Text(viewModel.testFeedback)
            .font(.system(size: 12))
            .foregroundStyle(.secondary)
          Text("系统是否显示横幅取决于 macOS 的通知与专注模式设置；同一下课瞬间的“下课”和“课间开始”自动合并为一条。")
            .font(.system(size: 12))
            .foregroundStyle(.secondary)
        }
      }
    }
    .onAppear {
      viewModel.refreshPermission()
    }
  }

  private func effectiveNotificationState(_ notif: NotificationSettings) -> (text: String, color: Color) {
    if !notif.enabled {
      return ("边界提醒已关闭", .secondary)
    }
    if notif.muted {
      return ("提醒已暂停——时间进度仍然继续，可随时恢复。", .orange)
    }
    if !viewModel.systemNotificationsEnabled {
      return ("系统通知未授权——请到系统设置允许 It's Class Time! 通知。", .red)
    }
    return ("提醒运行中：到边界时会主动请求系统通知。", .green)
  }
}

// MARK: - 4. 常规页

private struct GeneralSettingsPage: View {
  @ObservedObject private var services = Services.shared
  let onQuit: () -> Void

  var body: some View {
    let _ = services.uiRevision
    let cfg = services.config

    VStack(alignment: .leading, spacing: 14) {
      VStack(alignment: .leading, spacing: 4) {
        Text("常规")
          .font(.system(size: 26, weight: .semibold))
        Text("管理应用启动和本地数据。")
          .font(.system(size: 12.5))
          .foregroundStyle(.secondary)
      }

      ViewThatFits(in: .horizontal) {
        HStack(alignment: .top, spacing: 12) {
          leftColumn(cfg: cfg)
          aboutCard
            .frame(width: 270)
        }
        VStack(alignment: .leading, spacing: 12) {
          leftColumn(cfg: cfg)
          aboutCard
        }
      }

      HStack {
        Spacer()
        Button(role: .destructive, action: onQuit) {
          Text("退出应用")
        }
        .adaptiveGlassButton(prominent: false)
      }
    }
    .onAppear {
      services.refreshLaunchAtLoginStatus()
    }
  }

  @ViewBuilder
  private func leftColumn(cfg: AppConfig) -> some View {
    VStack(alignment: .leading, spacing: 12) {
      SettingsCard {
        VStack(alignment: .leading, spacing: 10) {
          Text("启动与窗口")
            .font(.system(size: 15.5, weight: .semibold))

          AlignedToggleRow(
            title: "登录时自动启动",
            isOn: Binding(
              get: { cfg.launchAtLogin },
              set: { val in
                services.setLaunchAtLogin(val)
              }
            )
          )

          if !services.launchAtLoginError.isEmpty {
            HStack(spacing: 8) {
              Text(services.launchAtLoginError)
                .font(.system(size: 12))
                .foregroundStyle(.orange)
              if services.launchAtLoginRequiresApproval {
                Button("打开登录项设置") {
                  services.openSystemLoginItemsSettings()
                }
                .controlSize(.small)
              }
            }
          }

          Text("关闭设置窗口时应用继续后台运行，提醒照常；菜单栏图标可重新打开设置或退出程序。")
            .font(.system(size: 12))
            .foregroundStyle(.secondary)
        }
      }

      SettingsCard {
        VStack(alignment: .leading, spacing: 10) {
          Text("本地数据")
            .font(.system(size: 15.5, weight: .semibold))

          Text("配置保存在此设备上，不需要登录账号。")
            .font(.system(size: 12))
            .foregroundStyle(.secondary)

          Text("配置文件夹：\(services.dataDirectory.path)")
            .font(.system(size: 12, design: .monospaced))
            .foregroundStyle(.secondary)
            .textSelection(.enabled)

          Button("打开配置文件夹") {
            NSWorkspace.shared.open(services.dataDirectory)
          }
          .adaptiveGlassButton(prominent: false)
        }
      }
    }
  }

  private var aboutCard: some View {
    SettingsCard {
      VStack(alignment: .leading, spacing: 10) {
        Text("关于“上课”时间到！")
          .font(.system(size: 15.5, weight: .semibold))
        Text("版本 \(appVersionString)")
          .font(.system(size: 13.5))
        Text("macOS · Swift + SwiftUI + AppKit")
          .font(.system(size: 12))
          .foregroundStyle(.secondary)
        Text(LiquidGlassCapabilities.capabilitySummary)
          .font(.system(size: 12))
          .foregroundStyle(Color.accentColor)
        Text("完全离线 · 无账号 · 无遥测")
          .font(.system(size: 12))
          .foregroundStyle(.secondary)
      }
    }
  }
}
