// macOS 顶部状态栏控制器（NSStatusItem）：
// - 遵循 Apple HIG Menu Bar Extras 规范：模板图标（isTemplate = true）实时绘制本段进度弧，
//   配合等宽数字（monospacedDigitSystemFont）在顶部状态栏展示倒计时/节次。
// - 左键点击：展开系统原生 Liquid Glass NSPopover（NSHostingController 自动精确测高，不裁切、不吞点击）。
// - 右键 / Control+点击：弹出系统原生 Liquid Glass NSMenu，随时可打开设置、切换形态或退出程序。
import AppKit
import ClassTimeEngine
import SwiftUI

@MainActor
final class MenuBarController: NSObject, NSPopoverDelegate, NSMenuDelegate {
  enum Command {
    case openSettings
    case toggleOverlay
    case viewBar
    case viewRing
    case viewScreenEdge
    case scopeCurrentBlock
    case scopeAllDay
    case menuBarDisplay(PresentationSettings.MenuBarDisplayMode)
    case glassStyle(PresentationSettings.GlassStyle)
    case toggleMute
    case quit
  }

  private struct IconCacheKey: Equatable {
    var progressStep: Int
    var active: Bool
    var muted: Bool
  }

  private var statusItem: NSStatusItem?
  private let onCommand: (Command) -> Void
  private let panelModel = MenuBarPanelModel()

  private var popover: NSPopover?
  private var popoverHosting: NSHostingController<MenuBarPopoverView>?
  private let contextMenu = NSMenu()

  // 图标缓存与低开销显示计时器（仅在浮窗隐藏且状态栏需实时读秒或面板展开时启用）
  private var lastIconKey: IconCacheKey?
  private var cachedIcon: NSImage?
  private var statusTimer: Timer?
  private var overlayIsTicking = false

  init(onCommand: @escaping (Command) -> Void) {
    self.onCommand = onCommand
    super.init()
    contextMenu.delegate = self
    setupStatusItem()
  }

  private func setupStatusItem() {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    item.autosaveName = "com.classtime.ItsClassTime.statusItem"
    item.menu = nil

    if let button = item.button {
      button.image = cachedTemplateIcon(progress: 0, active: false, muted: false)
      button.imagePosition = .imageLeading
      button.toolTip = "“上课”时间到！"
      button.setAccessibilityLabel("“上课”时间到！")
      button.target = self
      button.action = #selector(handleStatusBarClick(_:))
      button.sendAction(on: [.leftMouseUp, .rightMouseUp])
    }

    statusItem = item
    refresh()
    syncDisplayTimer()
  }

  func destroy() {
    stopStatusTimer()
    popover?.performClose(nil)
    popover?.close()
    popover = nil
    popoverHosting = nil
    if let item = statusItem {
      item.menu = nil
      NSStatusBar.system.removeStatusItem(item)
      statusItem = nil
    }
  }

  func setOverlayTicking(_ ticking: Bool) {
    overlayIsTicking = ticking
    syncDisplayTimer()
  }

  func refresh() {
    guard let button = statusItem?.button else { return }
    let cfg = Services.shared.config
    let pres = cfg.presentation
    let snap = Services.shared.snapshot
    let now = nowClock()
    let muted = cfg.notifications.muted

    let isActive = (snap.current != nil)
    let progress = isActive ? snap.progress : 0.0
    button.image = cachedTemplateIcon(progress: progress, active: isActive, muted: muted)

    let titleText = makeMenuBarTitle(mode: pres.menuBarDisplay, snapshot: snap, now: now)
    if titleText.isEmpty {
      button.attributedTitle = NSAttributedString(string: "")
      button.imagePosition = .imageOnly
    } else {
      let attrs: [NSAttributedString.Key: Any] = [
        .font: NSFont.monospacedDigitSystemFont(ofSize: 12.0, weight: .medium),
        .baselineOffset: -0.5,
      ]
      button.attributedTitle = NSAttributedString(string: " \(titleText)", attributes: attrs)
      button.imagePosition = .imageLeading
    }

    let summary = Formatters.statusSummary(snapshot: snap, muted: muted)
    button.toolTip = summary
    button.setAccessibilityTitle(summary)

    if popover?.isShown == true {
      panelModel.reload()
    }
    syncDisplayTimer()
  }

  // MARK: - 左键液态玻璃 Popover / 右键原生 NSMenu

  @objc private func handleStatusBarClick(_ sender: AnyObject?) {
    guard let event = NSApp.currentEvent else {
      togglePopover()
      return
    }
    let isContextTrigger =
      event.type == .rightMouseUp ||
      event.type == .rightMouseDown ||
      event.modifierFlags.contains(.control)

    if isContextTrigger {
      showContextMenu()
    } else {
      togglePopover()
    }
  }

  private func ensurePopover() -> NSPopover {
    if let existing = popover {
      return existing
    }
    let pop = NSPopover()
    pop.behavior = .transient
    pop.delegate = self

    let root = MenuBarPopoverView(model: panelModel) { [weak self] cmd in
      self?.dispatchCommand(cmd, fromPopover: true)
    }
    let hosting = NSHostingController(rootView: root)
    // 让 AppKit 根据 SwiftUI 内容精确计算弹窗尺寸，绝不裁切底部「打开设置… / 退出」。
    hosting.sizingOptions = [.preferredContentSize]
    pop.contentViewController = hosting

    popoverHosting = hosting
    popover = pop
    return pop
  }

  private func togglePopover() {
    guard let button = statusItem?.button else { return }
    let pop = ensurePopover()
    if pop.isShown {
      pop.performClose(nil)
      return
    }

    Services.shared.tickSnapshotOnly()
    panelModel.reload()
    pop.animates = !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion

    if let hosting = popoverHosting {
      hosting.view.layoutSubtreeIfNeeded()
      let fit = hosting.view.fittingSize
      if fit.width > 100 && fit.height > 100 {
        pop.contentSize = fit
      }
    }

    pop.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
    // 保持 .accessory 策略（不弹 Dock 图标），激活弹窗窗口使所有按钮首次点击即可响应。
    NSApp.activate(ignoringOtherApps: true)
    pop.contentViewController?.view.window?.makeKey()
    syncDisplayTimer()
  }

  func popoverDidClose(_ notification: Notification) {
    syncDisplayTimer()
  }

  private func showContextMenu() {
    guard let button = statusItem?.button else { return }
    if popover?.isShown == true {
      popover?.performClose(nil)
    }
    Services.shared.tickSnapshotOnly()
    rebuildContextMenu()

    // 使用非弃用的官方 NSMenu.popUp(positioning:at:in:) 直接在按钮下方弹出原生液态玻璃菜单，
    // 避免在按钮 action 回调内重入 button.performClick(nil)。
    let anchorY: CGFloat = button.isFlipped ? (button.bounds.maxY + 4) : (button.bounds.minY - 4)
    button.highlight(true)
    contextMenu.popUp(positioning: nil, at: NSPoint(x: 0, y: anchorY), in: button)
    button.highlight(false)
  }

  func menuDidClose(_ menu: NSMenu) {
    if menu === contextMenu {
      statusItem?.button?.highlight(false)
    }
  }

  private func rebuildContextMenu() {
    contextMenu.removeAllItems()
    let cfg = Services.shared.config
    let pres = cfg.presentation
    let snap = Services.shared.snapshot

    let statusLine = NSMenuItem(
      title: Formatters.statusSummary(snapshot: snap, muted: cfg.notifications.muted),
      action: nil,
      keyEquivalent: ""
    )
    statusLine.isEnabled = false
    contextMenu.addItem(statusLine)
    contextMenu.addItem(.separator())

    contextMenu.addItem(makeMenuItem("打开设置…", key: ",", tag: 1))
    contextMenu.addItem(makeMenuItem(pres.visible ? "隐藏桌面组件" : "显示桌面组件", checked: pres.visible, tag: 2))
    contextMenu.addItem(.separator())

    let viewLabel: String = switch pres.view {
    case .bar: "分节长条"
    case .ring: "圆环"
    case .screenEdge: "屏幕边框"
    }
    let viewItem = NSMenuItem(title: "显示形式 · \(viewLabel)", action: nil, keyEquivalent: "")
    let viewMenu = NSMenu(title: "显示形式")
    viewMenu.addItem(makeMenuItem("分节长条", checked: pres.view == .bar, tag: 3))
    viewMenu.addItem(makeMenuItem("圆环", checked: pres.view == .ring, tag: 4))
    viewMenu.addItem(makeMenuItem("屏幕边框", checked: pres.view == .screenEdge, tag: 5))
    viewItem.submenu = viewMenu
    contextMenu.addItem(viewItem)

    let scopeLabel = (pres.scope == .allDay) ? "全天" : "当前时段"
    let scopeItem = NSMenuItem(title: "显示范围 · \(scopeLabel)", action: nil, keyEquivalent: "")
    let scopeMenu = NSMenu(title: "显示范围")
    scopeMenu.addItem(makeMenuItem("当前时段", checked: pres.scope == .currentBlock, tag: 6))
    scopeMenu.addItem(makeMenuItem("全天", checked: pres.scope == .allDay, tag: 7))
    scopeItem.submenu = scopeMenu
    contextMenu.addItem(scopeItem)
    contextMenu.addItem(.separator())

    // 顶部状态栏显示子菜单
    let statusBarItem = NSMenuItem(title: "顶部状态栏显示", action: nil, keyEquivalent: "")
    let statusBarMenu = NSMenu(title: "顶部状态栏显示")
    statusBarMenu.addItem(makeMenuItem("仅图标", checked: pres.menuBarDisplay == .iconOnly, tag: 10))
    statusBarMenu.addItem(makeMenuItem("图标 + 剩余时间", checked: pres.menuBarDisplay == .countdown, tag: 11))
    statusBarMenu.addItem(makeMenuItem("图标 + 节次与剩余时间", checked: pres.menuBarDisplay == .detailed, tag: 12))
    statusBarItem.submenu = statusBarMenu
    contextMenu.addItem(statusBarItem)

    if LiquidGlassCapabilities.isAvailable {
      let glassItem = NSMenuItem(title: "液态玻璃通透度", action: nil, keyEquivalent: "")
      let glassMenu = NSMenu(title: "液态玻璃通透度")
      glassMenu.addItem(makeMenuItem("通透玻璃（推荐）", checked: pres.glassStyle == .clear, tag: 13))
      glassMenu.addItem(makeMenuItem("标准玻璃", checked: pres.glassStyle == .regular, tag: 14))
      glassItem.submenu = glassMenu
      contextMenu.addItem(glassItem)
    }

    contextMenu.addItem(.separator())
    contextMenu.addItem(makeMenuItem("暂停提醒", checked: cfg.notifications.muted, tag: 8))
    contextMenu.addItem(.separator())
    contextMenu.addItem(makeMenuItem("退出 It's Class Time!", key: "q", tag: 9))
  }

  private func makeMenuItem(
    _ title: String,
    checked: Bool = false,
    key: String = "",
    tag: Int
  ) -> NSMenuItem {
    let item = NSMenuItem(title: title, action: #selector(handleContextMenuItem(_:)), keyEquivalent: key)
    item.target = self
    item.tag = tag
    item.state = checked ? .on : .off
    if !key.isEmpty {
      item.keyEquivalentModifierMask = [.command]
    }
    return item
  }

  @objc private func handleContextMenuItem(_ sender: NSMenuItem) {
    let command: Command? = switch sender.tag {
    case 1: .openSettings
    case 2: .toggleOverlay
    case 3: .viewBar
    case 4: .viewRing
    case 5: .viewScreenEdge
    case 6: .scopeCurrentBlock
    case 7: .scopeAllDay
    case 8: .toggleMute
    case 9: .quit
    case 10: .menuBarDisplay(.iconOnly)
    case 11: .menuBarDisplay(.countdown)
    case 12: .menuBarDisplay(.detailed)
    case 13: .glassStyle(.clear)
    case 14: .glassStyle(.regular)
    default: nil
    }
    if let command {
      dispatchCommand(command, fromPopover: false)
    }
  }

  private func dispatchCommand(_ command: Command, fromPopover: Bool) {
    switch command {
    case .openSettings, .quit:
      if fromPopover {
        popover?.performClose(nil)
      }
      // 延后到下一轮主循环，等弹窗/菜单关闭动画完成再切换激活策略或退出，避免焦点竞争。
      DispatchQueue.main.async { [weak self] in
        self?.onCommand(command)
      }
    default:
      onCommand(command)
      panelModel.reload()
      refresh()
    }
  }

  // MARK: - 仅表现层按需刷新（绝不驱动通知，浮窗已计时时不重复建 Timer）

  private func syncDisplayTimer() {
    let mode = Services.shared.config.presentation.menuBarDisplay
    let popoverShown = (popover?.isShown == true)
    let needsLiveSeconds = popoverShown || (mode != .iconOnly)

    if needsLiveSeconds && !overlayIsTicking {
      startStatusTimer()
    } else {
      stopStatusTimer()
    }
  }

  private func startStatusTimer() {
    guard statusTimer == nil else { return }
    let timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
      Task { @MainActor in
        Services.shared.tickSnapshotOnly()
        self?.refresh()
      }
    }
    timer.tolerance = 0.2
    statusTimer = timer
  }

  private func stopStatusTimer() {
    statusTimer?.invalidate()
    statusTimer = nil
  }

  // MARK: - 顶部状态栏文案与动态进度模板图标

  private func makeMenuBarTitle(
    mode: PresentationSettings.MenuBarDisplayMode,
    snapshot: Snapshot,
    now: WallClock
  ) -> String {
    guard mode != .iconOnly else { return "" }

    if let cur = snapshot.current {
      let remaining = (cur.end.totalMinutes - now.minutesSinceMidnight) * 60 - now.second
      let clockText = Formatters.remainingClock(remaining)
      switch mode {
      case .iconOnly:
        return ""
      case .countdown:
        switch cur.kind {
        case .focus:
          return clockText
        case .shortBreak:
          return "课间 \(clockText)"
        case .longBreak:
          return "休息 \(clockText)"
        case .lunch:
          return "午休 \(clockText)"
        }
      case .detailed:
        switch cur.kind {
        case .focus:
          return "第\(cur.focusOrdinal)节 \(clockText)"
        case .shortBreak:
          return "课间 \(clockText)"
        case .longBreak:
          return "长休 \(clockText)"
        case .lunch:
          return "午休 \(clockText)"
        }
      }
    }

    if let next = snapshot.next {
      return mode == .detailed
        ? "下段 \(Formatters.clock(next.start))"
        : Formatters.clock(next.start)
    }

    if snapshot.hasSchedule {
      return mode == .detailed ? "今日已结束" : "已结束"
    }
    return mode == .detailed ? "无安排" : ""
  }

  private func cachedTemplateIcon(progress: Double, active: Bool, muted: Bool) -> NSImage {
    let clamped = max(0.0, min(1.0, progress))
    let step = active ? Int((clamped * 72.0).rounded()) : 0
    let key = IconCacheKey(progressStep: step, active: active, muted: muted)
    if key == lastIconKey, let cachedIcon {
      return cachedIcon
    }
    let image = Self.makeTemplateIcon(progress: Double(step) / 72.0, active: active, muted: muted)
    lastIconKey = key
    cachedIcon = image
    return image
  }

  private static func makeTemplateIcon(progress: Double, active: Bool, muted: Bool) -> NSImage {
    let size = NSSize(width: 18, height: 18)
    let image = NSImage(size: size, flipped: false) { rect in
      let center = NSPoint(x: rect.midX, y: rect.midY)
      let radius: CGFloat = 6.8

      // 外圈底环（细线）
      let baseRing = NSBezierPath()
      baseRing.appendArc(withCenter: center, radius: radius, startAngle: 0, endAngle: 360)
      baseRing.lineWidth = 1.35
      NSColor.labelColor.withAlphaComponent(0.36).setStroke()
      baseRing.stroke()

      if active {
        // 实时进度主弧：从 12 点钟顺时针生长，直觉反映本段进度
        let sweep = max(12.0, min(360.0, progress * 360.0))
        let progressArc = NSBezierPath()
        progressArc.appendArc(
          withCenter: center,
          radius: radius,
          startAngle: 90,
          endAngle: 90 - CGFloat(sweep),
          clockwise: true
        )
        progressArc.lineWidth = 2.05
        progressArc.lineCapStyle = .round
        NSColor.labelColor.setStroke()
        progressArc.stroke()

        // 中心细指针（保持轻盈辨识度）
        let hands = NSBezierPath()
        hands.move(to: NSPoint(x: center.x, y: center.y + 3.1))
        hands.line(to: center)
        hands.line(to: NSPoint(x: center.x + 2.4, y: center.y - 1.1))
        hands.lineWidth = 1.4
        hands.lineCapStyle = .round
        hands.lineJoinStyle = .round
        NSColor.labelColor.withAlphaComponent(0.90).setStroke()
        hands.stroke()
      } else {
        // 空闲/未开始：标准时钟弧与表针
        let idleArc = NSBezierPath()
        idleArc.appendArc(
          withCenter: center,
          radius: radius,
          startAngle: 90,
          endAngle: -145,
          clockwise: true
        )
        idleArc.lineWidth = 1.85
        idleArc.lineCapStyle = .round
        NSColor.labelColor.setStroke()
        idleArc.stroke()

        let hands = NSBezierPath()
        hands.move(to: NSPoint(x: center.x - 2.3, y: center.y + 1.8))
        hands.line(to: center)
        hands.line(to: NSPoint(x: center.x + 3.1, y: center.y + 1.7))
        hands.lineWidth = 1.5
        hands.lineCapStyle = .round
        hands.lineJoinStyle = .round
        hands.stroke()
      }

      if muted {
        let slash = NSBezierPath()
        slash.move(to: NSPoint(x: 3.0, y: 15.0))
        slash.line(to: NSPoint(x: 15.0, y: 3.0))
        slash.lineWidth = 1.55
        slash.lineCapStyle = .round
        NSColor.labelColor.setStroke()
        slash.stroke()
      }
      return true
    }
    image.isTemplate = true
    return image
  }
}
