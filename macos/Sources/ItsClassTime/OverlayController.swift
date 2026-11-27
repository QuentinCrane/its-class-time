// 悬浮窗控制器：使用官方 NSPanel (.nonactivatingPanel + .floating) 实现不抢焦点置顶、
// 四边拖拽吸附、连续三击归中、自动收起把手、悬停详情弹卡与屏幕边框模式。
import AppKit
import ClassTimeEngine
import QuartzCore
import SwiftUI

private final class NonActivatingPanel: NSPanel {
  var allowFullScreenFrame: Bool = false
  var onMouseDownEvent: ((NSEvent) -> Void)?
  var onMouseDraggedEvent: ((NSEvent) -> Void)?
  var onMouseUpEvent: ((NSEvent) -> Void)?

  init(
    contentRect: NSRect,
    styleMask style: NSWindow.StyleMask,
    backing backingStoreType: NSWindow.BackingStoreType,
    defer flag: Bool,
    allowFullScreenFrame: Bool = false
  ) {
    self.allowFullScreenFrame = allowFullScreenFrame
    super.init(contentRect: contentRect, styleMask: style, backing: backingStoreType, defer: flag)
  }

  override var canBecomeKey: Bool { false }
  override var canBecomeMain: Bool { false }

  override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect {
    allowFullScreenFrame ? frameRect : super.constrainFrameRect(frameRect, to: screen)
  }

  override func sendEvent(_ event: NSEvent) {
    switch event.type {
    case .leftMouseDown:
      onMouseDownEvent?(event)
    case .leftMouseDragged:
      onMouseDraggedEvent?(event)
    case .leftMouseUp:
      onMouseUpEvent?(event)
    default:
      break
    }
    super.sendEvent(event)
  }
}

@MainActor
final class OverlayController {
  private static let barWidth: CGFloat = 680
  private static let barHeight: CGFloat = 32
  private static let sideBarThickness: CGFloat = 34
  private static let sideBarLength: CGFloat = 420
  private static let ringDiameter: CGFloat = 156
  private static let snapInset: CGFloat = 12
  private static let handleLength: CGFloat = 84
  private static let handleThickness: CGFloat = 12
  private static let detailWidth: CGFloat = 280
  private static let detailGap: CGFloat = 8

  private let stateModel = OverlayStateModel()
  private let detailModel = HoverDetailModel()

  private var mainPanel: NonActivatingPanel?
  private var mainGlassHost: GlassPanelHost<OverlayRootView>?
  private var detailPanel: NonActivatingPanel?
  private var detailGlassHost: GlassPanelHost<HoverDetailCardView>?
  private var screenEdgePanel: NonActivatingPanel?
  private var screenEdgeHosting: NSHostingView<ScreenEdgePerimeterView>?
  private var lastMainCornerRadius: CGFloat = -1

  var onDisplayTick: (() -> Void)?

  private var refreshTimer: Timer?
  private var collapseWorkItem: DispatchWorkItem?

  private(set) var isVisible: Bool = false
  private var edgeCollapsed: Bool = false
  private var displayedView: PresentationSettings.ViewMode?
  private var displayedEdge: PresentationSettings.Edge?
  private var displayedCollapsed: Bool?
  private var isTransitioning: Bool = false
  private var transitionGeneration: Int = 0
  private var hoveredSegmentId: String?
  private var hoveredAnchorRect: CGRect = .zero

  // 拖拽与三击追踪
  private var isDragging = false
  private var dragMouseStart: NSPoint = .zero
  private var dragFrameOriginStart: NSPoint = .zero
  private var pointerDown = false
  private var lastPressTimestamp: TimeInterval = 0
  private var lastPressLocation: NSPoint = .zero
  private var pressCount: Int = 0

  init() {
    createPanels()
    NotificationCenter.default.addObserver(
      forName: NSApplication.didChangeScreenParametersNotification,
      object: nil,
      queue: .main
    ) { [weak self] _ in
      Task { @MainActor in
        self?.handleScreenParametersChanged()
      }
    }
    NSWorkspace.shared.notificationCenter.addObserver(
      forName: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification,
      object: nil,
      queue: .main
    ) { [weak self] _ in
      Task { @MainActor in
        self?.refreshGlassSurface()
      }
    }
  }

  func close() {
    hideOverlay()
    mainPanel?.close()
    detailPanel?.close()
    screenEdgePanel?.close()
    mainPanel = nil
    mainGlassHost = nil
    detailPanel = nil
    detailGlassHost = nil
    screenEdgePanel = nil
  }

  private func createPanels() {
    stateModel.onSegmentHover = { [weak self] segId, anchorRect in
      self?.handleSegmentHover(segmentId: segId, anchorRect: anchorRect)
    }
    stateModel.onPanelHover = { [weak self] inside in
      self?.handlePanelHover(inside: inside)
    }

    let initialBarRect = NSRect(x: 0, y: 0, width: Self.barWidth, height: Self.barHeight)
    let panel = NonActivatingPanel(
      contentRect: initialBarRect,
      styleMask: [.borderless, .nonactivatingPanel],
      backing: .buffered,
      defer: false
    )
    panel.title = "ClassTime Overlay"
    panel.level = .floating
    panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
    panel.isOpaque = false
    panel.backgroundColor = .clear
    panel.hasShadow = true
    panel.hidesOnDeactivate = false
    panel.acceptsMouseMovedEvents = true
    panel.isMovable = false

    let isClearGlass = (Services.shared.config.presentation.glassStyle == .clear)
    let mHost = GlassPanelHost(
      rootView: OverlayRootView(model: stateModel),
      frame: initialBarRect,
      cornerRadius: Self.barHeight / 2.0,
      interactive: true,
      clearStyle: isClearGlass
    )
    panel.contentView = mHost.rootView
    mainGlassHost = mHost

    panel.onMouseDownEvent = { [weak self] ev in self?.handleMouseDown(ev) }
    panel.onMouseDraggedEvent = { [weak self] ev in self?.handleMouseDragged(ev) }
    panel.onMouseUpEvent = { [weak self] ev in self?.handleMouseUp(ev) }
    mainPanel = panel

    let initialDetailRect = NSRect(x: 0, y: 0, width: Self.detailWidth, height: 152)
    let dPanel = NonActivatingPanel(
      contentRect: initialDetailRect,
      styleMask: [.borderless, .nonactivatingPanel],
      backing: .buffered,
      defer: true
    )
    dPanel.title = "ClassTime Detail"
    dPanel.level = .floating
    dPanel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
    dPanel.isOpaque = false
    dPanel.backgroundColor = .clear
    dPanel.hasShadow = true
    dPanel.hidesOnDeactivate = false
    dPanel.ignoresMouseEvents = true

    let dHost = GlassPanelHost(
      rootView: HoverDetailCardView(model: detailModel),
      frame: initialDetailRect,
      cornerRadius: 18.0,
      interactive: false,
      clearStyle: isClearGlass
    )
    dPanel.contentView = dHost.rootView
    detailGlassHost = dHost
    detailPanel = dPanel
  }

  // MARK: - 显隐与状态刷新

  func showOverlay() {
    isVisible = true
    applyPresentation(animate: false)
    startTimer()
    applyState()
  }

  func hideOverlay() {
    isVisible = false
    transitionGeneration &+= 1
    isTransitioning = false
    displayedView = nil
    displayedEdge = nil
    displayedCollapsed = nil
    cancelCollapseTimer()
    stopTimer()
    hideDetail()
    mainPanel?.alphaValue = 1.0
    mainPanel?.orderOut(nil)
    screenEdgePanel?.alphaValue = 1.0
    screenEdgePanel?.orderOut(nil)
  }

  func applyPresentation(animate: Bool = true) {
    let pres = Services.shared.config.presentation
    guard isVisible && pres.visible else {
      transitionGeneration &+= 1
      isTransitioning = false
      displayedView = nil
      displayedEdge = nil
      displayedCollapsed = nil
      mainPanel?.alphaValue = 1.0
      mainPanel?.orderOut(nil)
      screenEdgePanel?.alphaValue = 1.0
      screenEdgePanel?.orderOut(nil)
      hideDetail()
      stopTimer()
      return
    }

    startTimer()
    let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
    let prevView = displayedView
    let prevEdge = displayedEdge
    let prevCollapsed = displayedCollapsed

    // —— 切换到屏幕边框 ——
    if pres.view == .screenEdge {
      hideDetail()
      cancelCollapseTimer()
      transitionGeneration &+= 1
      let gen = transitionGeneration
      let hadMainVisible = (mainPanel?.isVisible == true)
      displayedView = .screenEdge
      displayedEdge = pres.edge
      displayedCollapsed = false

      let shouldFadeInEdge = animate && !reduceMotion && hadMainVisible && prevView != .screenEdge
      refreshScreenEdge(orderFront: !shouldFadeInEdge)
      if shouldFadeInEdge {
        isTransitioning = true
        screenEdgePanel?.alphaValue = 0.0
        screenEdgePanel?.orderFrontRegardless()
        NSAnimationContext.runAnimationGroup({ ctx in
          ctx.duration = 0.18
          ctx.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
          mainPanel?.animator().alphaValue = 0.0
          screenEdgePanel?.animator().alphaValue = 1.0
        }, completionHandler: { [weak self] in
          Task { @MainActor in
            guard let self, self.transitionGeneration == gen else { return }
            self.mainPanel?.orderOut(nil)
            self.mainPanel?.alphaValue = 1.0
            self.isTransitioning = false
          }
        })
      } else {
        isTransitioning = false
        mainPanel?.orderOut(nil)
        mainPanel?.alphaValue = 1.0
        screenEdgePanel?.alphaValue = 1.0
        screenEdgePanel?.orderFrontRegardless()
      }
      return
    }

    // —— 离开屏幕边框或常规条/环模式 ——
    let leavingScreenEdge = (prevView == .screenEdge) || (screenEdgePanel?.isVisible == true)
    if !pres.autoHideAtEdge && edgeCollapsed {
      edgeCollapsed = false
    } else if pres.autoHideAtEdge && mainPanel?.isVisible != true {
      edgeCollapsed = true
    }

    let targetFrame = computeLayoutRect(presentation: pres, collapsed: edgeCollapsed)
    let viewChanged = (prevView != nil && prevView != pres.view)
    let orientationChanged = (prevEdge != nil && prevEdge!.isSide != pres.edge.isSide)
    let collapseChanged = (prevCollapsed != nil && prevCollapsed != edgeCollapsed)
    // 形态切换（长条↔圆环）、横竖转向或展开↔收起把手：
    // 避免 NSPanel 直接做非等比缩放导致中间帧出现圆角矩形并让 NSGlassEffectView 掉帧卡顿，
    // 改为先淡出旧形态，在 alpha == 0 时一次性落位新尺寸与精确圆角并重算阴影，再平滑淡入。
    let needsShapeMorph = viewChanged || orientationChanged || collapseChanged

    guard let panel = mainPanel else { return }

    if leavingScreenEdge {
      transitionGeneration &+= 1
      let gen = transitionGeneration
      commitMainPanelGeometry(panel: panel, targetFrame: targetFrame, presentation: pres, collapsed: edgeCollapsed)
      if animate && !reduceMotion {
        isTransitioning = true
        panel.alphaValue = 0.0
        panel.orderFrontRegardless()
        NSAnimationContext.runAnimationGroup({ ctx in
          ctx.duration = 0.18
          ctx.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
          screenEdgePanel?.animator().alphaValue = 0.0
          panel.animator().alphaValue = 1.0
        }, completionHandler: { [weak self] in
          Task { @MainActor in
            guard let self, self.transitionGeneration == gen else { return }
            self.screenEdgePanel?.orderOut(nil)
            self.screenEdgePanel?.alphaValue = 1.0
            self.isTransitioning = false
            self.finishPresentationUpdate(presentation: pres)
          }
        })
      } else {
        isTransitioning = false
        screenEdgePanel?.orderOut(nil)
        screenEdgePanel?.alphaValue = 1.0
        panel.alphaValue = 1.0
        panel.orderFrontRegardless()
        finishPresentationUpdate(presentation: pres)
      }
      return
    }

    if needsShapeMorph && animate && panel.isVisible && !reduceMotion && !isDragging {
      hideDetail()
      transitionGeneration &+= 1
      let gen = transitionGeneration
      isTransitioning = true
      NSAnimationContext.runAnimationGroup({ ctx in
        ctx.duration = 0.09
        ctx.timingFunction = CAMediaTimingFunction(name: .easeIn)
        panel.animator().alphaValue = 0.0
      }, completionHandler: { [weak self] in
        Task { @MainActor in
          guard let self, self.transitionGeneration == gen, let panel = self.mainPanel else { return }
          let latestPres = Services.shared.config.presentation
          let latestFrame = self.computeLayoutRect(presentation: latestPres, collapsed: self.edgeCollapsed)
          self.commitMainPanelGeometry(
            panel: panel,
            targetFrame: latestFrame,
            presentation: latestPres,
            collapsed: self.edgeCollapsed
          )
          // 在 alpha == 0 状态下冲刷事务后，于下一主循环再淡入，确保 WindowServer 首帧即呈现精确圆角与玻璃折射。
          DispatchQueue.main.async { [weak self] in
            guard let self, self.transitionGeneration == gen, let panel = self.mainPanel else { return }
            NSAnimationContext.runAnimationGroup({ ctx in
              ctx.duration = 0.14
              ctx.timingFunction = CAMediaTimingFunction(name: .easeOut)
              panel.animator().alphaValue = 1.0
            }, completionHandler: { [weak self] in
              Task { @MainActor in
                guard let self, self.transitionGeneration == gen else { return }
                self.isTransitioning = false
                self.finishPresentationUpdate(presentation: latestPres)
              }
            })
          }
        }
      })
      return
    }

    // 同形态平移或微调长度（如沿边归中、切换紧贴边缘、拖动同向换边、调整长条长度）
    transitionGeneration &+= 1
    let gen = transitionGeneration
    if animate && panel.isVisible && !reduceMotion && !isDragging && !needsShapeMorph && panel.frame != targetFrame {
      isTransitioning = true
      stateModel.collapsed = edgeCollapsed
      stateModel.reload()
      displayedView = pres.view
      displayedEdge = pres.edge
      displayedCollapsed = edgeCollapsed
      panel.hasShadow = LiquidGlassCapabilities.isAvailable
        ? !edgeCollapsed
        : (!pres.flushWithEdge && !edgeCollapsed)
      updateMainGlassSurface(targetFrame: targetFrame, presentation: pres, collapsed: edgeCollapsed, syncSize: false)
      NSAnimationContext.runAnimationGroup({ ctx in
        ctx.duration = 0.20
        ctx.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        panel.animator().setFrame(targetFrame, display: true)
      }, completionHandler: { [weak self] in
        Task { @MainActor in
          guard let self, self.transitionGeneration == gen, let panel = self.mainPanel else { return }
          self.isTransitioning = false
          self.updateMainGlassSurface(targetFrame: targetFrame, presentation: pres, collapsed: self.edgeCollapsed, syncSize: true)
          panel.invalidateShadow()
          self.finishPresentationUpdate(presentation: pres)
        }
      })
      return
    }

    // 首次显示 / 减少动态效果 / 拖拽落位：先在透明态完成布局与圆角，避免闪现旧形
    isTransitioning = false
    let wasHidden = !panel.isVisible
    if wasHidden {
      panel.alphaValue = 0.0
    }
    commitMainPanelGeometry(panel: panel, targetFrame: targetFrame, presentation: pres, collapsed: edgeCollapsed)
    panel.alphaValue = 1.0
    if wasHidden {
      panel.orderFrontRegardless()
    }
    finishPresentationUpdate(presentation: pres)
  }

  private func commitMainPanelGeometry(
    panel: NonActivatingPanel,
    targetFrame: NSRect,
    presentation: PresentationSettings,
    collapsed: Bool
  ) {
    CATransaction.begin()
    CATransaction.setDisableActions(true)
    stateModel.collapsed = collapsed
    stateModel.reload()
    displayedView = presentation.view
    displayedEdge = presentation.edge
    displayedCollapsed = collapsed
    panel.hasShadow = LiquidGlassCapabilities.isAvailable
      ? !collapsed
      : (!presentation.flushWithEdge && !collapsed)
    panel.setFrame(targetFrame, display: false)
    updateMainGlassSurface(targetFrame: targetFrame, presentation: presentation, collapsed: collapsed, syncSize: true)
    panel.contentView?.layoutSubtreeIfNeeded()
    panel.displayIfNeeded()
    panel.invalidateShadow()
    CATransaction.commit()
    CATransaction.flush()
  }

  private func finishPresentationUpdate(presentation: PresentationSettings) {
    if presentation.autoHideAtEdge && !edgeCollapsed {
      queueEdgeCollapse()
    }
    onDisplayTick?()
  }

  func applyState() {
    guard isVisible && Services.shared.config.presentation.visible else { return }
    Services.shared.tickSnapshotOnly()
    onDisplayTick?()

    // 形态切换动画期间不抢先重载视图或用旧 frame 覆盖目标圆角，等待切换落位后再同步。
    guard !isTransitioning else { return }

    stateModel.collapsed = edgeCollapsed
    stateModel.reload()

    let pres = Services.shared.config.presentation
    if pres.view == .screenEdge {
      refreshScreenEdge()
      return
    }

    // 复用可见浮窗的刷新节奏：显示器拔插或主屏变化后，若浮窗不在目标工作区内则自动复位。
    if !isDragging && !pointerDown,
       let panel = mainPanel,
       let targetScreen = DisplayCatalog.findScreen(id: pres.monitorId) {
      let currentScreenId = panel.screen.map { DisplayCatalog.screenIdentifier($0) }
      let targetScreenId = DisplayCatalog.screenIdentifier(targetScreen)
      if currentScreenId != targetScreenId || !panel.frame.intersects(targetScreen.visibleFrame) {
        hideDetail()
        let rect = computeLayoutRect(presentation: pres, collapsed: edgeCollapsed)
        commitMainPanelGeometry(panel: panel, targetFrame: rect, presentation: pres, collapsed: edgeCollapsed)
      } else {
        updateMainGlassSurface(targetFrame: panel.frame, presentation: pres, collapsed: edgeCollapsed)
      }
    }

    if hoveredSegmentId != nil {
      refreshDetailData()
    }
  }

  private func refreshGlassSurface() {
    let pres = Services.shared.config.presentation
    let isClear = (pres.glassStyle == .clear)
    if let panel = mainPanel, pres.view != .screenEdge {
      updateMainGlassSurface(targetFrame: panel.frame, presentation: pres, collapsed: edgeCollapsed)
      panel.invalidateShadow()
    }
    if let data = detailModel.data {
      detailGlassHost?.updateAppearance(
        cornerRadius: 18.0,
        tintColor: isClear ? nil : WidgetPalette.kindNSColor(data.kind).withAlphaComponent(0.06),
        interactive: false,
        clearStyle: isClear
      )
      detailPanel?.invalidateShadow()
    }
  }

  private func updateMainGlassSurface(
    targetFrame: NSRect,
    presentation: PresentationSettings,
    collapsed: Bool,
    syncSize: Bool = true
  ) {
    let minSide = min(targetFrame.width, targetFrame.height)
    let cornerRadius: CGFloat
    if collapsed || presentation.view == .ring {
      cornerRadius = minSide / 2.0
    } else if presentation.flushWithEdge {
      cornerRadius = min(11.0, minSide / 2.0)
    } else {
      cornerRadius = minSide / 2.0
    }

    let isClear = (presentation.glassStyle == .clear)
    let hasCustomBarBg = (presentation.view == .bar && !collapsed && !presentation.barBackground.isEmpty)
    let tint: NSColor? = if !isClear && !hasCustomBarBg, let current = Services.shared.snapshot.current {
      WidgetPalette.kindNSColor(current.kind).withAlphaComponent(0.06)
    } else {
      nil
    }

    mainGlassHost?.updateAppearance(
      size: syncSize ? targetFrame.size : nil,
      cornerRadius: cornerRadius,
      tintColor: tint,
      interactive: true,
      clearStyle: isClear
    )
    if abs(lastMainCornerRadius - cornerRadius) > 0.25 {
      lastMainCornerRadius = cornerRadius
      mainPanel?.invalidateShadow()
    }
  }

  private func handleScreenParametersChanged() {
    guard isVisible else { return }
    applyPresentation(animate: false)
  }

  // MARK: - 屏幕边框模式

  private func refreshScreenEdge(orderFront: Bool = true) {
    let pres = Services.shared.config.presentation
    guard let screen = DisplayCatalog.findScreen(id: pres.monitorId) else {
      screenEdgePanel?.orderOut(nil)
      return
    }
    // 屏幕边框沿目标显示器完整四边（screen.frame）贴合绘制，包含菜单栏与 Dock 边缘区域；
    // 面板采用官方 .statusBar 层级并完全穿透鼠标点击，不阻挡菜单栏、Dock 或下拉菜单交互。
    let fullFrame = screen.frame
    guard fullFrame.width > 10 && fullFrame.height > 10 else {
      screenEdgePanel?.orderOut(nil)
      return
    }

    let now = nowClock()
    let plan = Services.shared.plan(for: now)
    let state = buildWidgetState(segments: plan.segments, now: now, scope: pres.scope)
    let color: Color = if let current = state.snapshot.current {
      WidgetPalette.kindColor(current.kind)
    } else {
      .accentColor
    }

    let rootView = ScreenEdgePerimeterView(
      segments: plan.segments,
      now: now,
      scope: pres.scope,
      colorMode: pres.screenEdgeColors,
      progress: state.edgeProgress,
      thickness: CGFloat(pres.screenEdgeThickness),
      color: color
    )

    if screenEdgePanel == nil {
      let sPanel = NonActivatingPanel(
        contentRect: fullFrame,
        styleMask: [.borderless, .nonactivatingPanel],
        backing: .buffered,
        defer: false,
        allowFullScreenFrame: true
      )
      sPanel.title = "ClassTime Screen Edge"
      sPanel.level = .statusBar
      sPanel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
      sPanel.isOpaque = false
      sPanel.backgroundColor = .clear
      sPanel.hasShadow = false
      sPanel.hidesOnDeactivate = false
      sPanel.ignoresMouseEvents = true
      let host = NSHostingView(rootView: rootView)
      host.frame = NSRect(origin: .zero, size: fullFrame.size)
      host.autoresizingMask = [.width, .height]
      sPanel.contentView = host
      screenEdgeHosting = host
      screenEdgePanel = sPanel
    } else {
      screenEdgeHosting?.rootView = rootView
    }

    if let sPanel = screenEdgePanel {
      if sPanel.frame != fullFrame {
        sPanel.setFrame(fullFrame, display: true)
      }
      if orderFront && !sPanel.isVisible {
        sPanel.orderFrontRegardless()
      }
    }
  }

  // MARK: - 悬停详情与贴边自动收起

  private func handlePanelHover(inside: Bool) {
    if inside {
      cancelCollapseTimer()
      if edgeCollapsed {
        setEdgeCollapsed(false, animate: true)
      }
    } else {
      hideDetail()
      queueEdgeCollapse()
    }
  }

  private func handleSegmentHover(segmentId: String?, anchorRect: CGRect) {
    guard !isDragging && !pointerDown && !edgeCollapsed && isVisible else { return }
    guard let segmentId, !segmentId.isEmpty else {
      hideDetail()
      return
    }
    hoveredSegmentId = segmentId
    stateModel.hoveredSegmentId = segmentId
    hoveredAnchorRect = anchorRect
    refreshDetailData()
    positionAndShowDetail(anchorRect: anchorRect)
  }

  private func refreshDetailData() {
    guard let segId = hoveredSegmentId else {
      hideDetail()
      return
    }
    let sorted = sortedByStart(Services.shared.currentEffectiveSegments)
    guard let index = sorted.firstIndex(where: { $0.id == segId }) else {
      hideDetail()
      return
    }
    let selected = sorted[index]
    let now = nowClock()
    let duration = max(1, selected.durationMinutes * 60)
    let rawElapsed = (now.minutesSinceMidnight - selected.start.totalMinutes) * 60 + now.second
    let elapsed = max(0, min(duration, rawElapsed))
    let progress = Double(elapsed) / Double(duration)

    var title: String
    if selected.kind == .focus {
      let ordinal = sorted[0...index].filter { $0.kind == .focus }.count
      title = "第 \(ordinal) 节"
    } else {
      title = Formatters.segmentKindLabel(selected.kind)
    }

    if elapsed >= duration {
      title += " · 已结束"
    } else if now.minutesSinceMidnight >= selected.start.totalMinutes {
      title += " · 进行中"
    } else {
      title += " · 尚未开始"
    }

    let kindLabel = Formatters.segmentKindLabel(selected.kind)
    let rangeStr = Formatters.range(selected)
    let generic = (selected.label == kindLabel) || title.hasPrefix(selected.label)
    let rangeLine = generic ? rangeStr : "\(selected.label)  \(rangeStr)"

    let timingLine =
      "已用 \(Formatters.elapsed(elapsed)) / \(Formatters.elapsed(duration))    剩余 \(Formatters.elapsed(duration - elapsed))"

    let nextLine: String
    if index + 1 < sorted.count {
      let nxt = sorted[index + 1]
      nextLine = "下一段  \(Formatters.clock(nxt.start))  \(nxt.label)"
    } else {
      nextLine = "这是今天最后一段"
    }

    detailModel.data = HoverDetailCardData(
      title: title,
      rangeText: rangeLine,
      percentText: Formatters.percent(progress),
      progress: progress,
      timingText: timingLine,
      nextText: nextLine,
      muted: Services.shared.config.notifications.muted,
      kind: selected.kind
    )
    let isClear = (Services.shared.config.presentation.glassStyle == .clear)
    detailGlassHost?.updateAppearance(
      cornerRadius: 18.0,
      tintColor: isClear ? nil : WidgetPalette.kindNSColor(selected.kind).withAlphaComponent(0.06),
      interactive: false,
      clearStyle: isClear
    )
  }

  private func positionAndShowDetail(anchorRect: CGRect) {
    guard let dPanel = detailPanel, let mPanel = mainPanel else { return }
    let hosting = detailGlassHost?.hostingView
    hosting?.layoutSubtreeIfNeeded()
    let fitting = hosting?.fittingSize ?? NSSize(width: Self.detailWidth, height: 152)
    let cardSize = NSSize(width: Self.detailWidth, height: max(130, fitting.height))

    let barFrame = mPanel.frame
    let screen = mPanel.screen ?? DisplayCatalog.primaryScreen
    let work = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
    let edge = Services.shared.config.presentation.edge

    // AppKit 坐标原点在左下角：悬停卡片永远向屏幕内侧展开。
    var origin = NSPoint.zero
    switch edge {
    case .bottom:
      origin = NSPoint(
        x: anchorRect.midX - cardSize.width / 2,
        y: barFrame.maxY + Self.detailGap
      )
    case .top:
      origin = NSPoint(
        x: anchorRect.midX - cardSize.width / 2,
        y: barFrame.minY - Self.detailGap - cardSize.height
      )
    case .left:
      origin = NSPoint(
        x: barFrame.maxX + Self.detailGap,
        y: anchorRect.midY - cardSize.height / 2
      )
    case .right:
      origin = NSPoint(
        x: barFrame.minX - Self.detailGap - cardSize.width,
        y: anchorRect.midY - cardSize.height / 2
      )
    }

    let clamped = clampToWorkArea(origin: origin, size: cardSize, work: work)
    dPanel.setFrame(NSRect(origin: clamped, size: cardSize), display: true)
    dPanel.invalidateShadow()
    if !dPanel.isVisible {
      dPanel.orderFrontRegardless()
    }
  }

  private func hideDetail() {
    hoveredSegmentId = nil
    stateModel.hoveredSegmentId = nil
    detailModel.data = nil
    detailPanel?.orderOut(nil)
  }

  private func queueEdgeCollapse() {
    cancelCollapseTimer()
    let pres = Services.shared.config.presentation
    guard isVisible && pres.visible && pres.view != .screenEdge && pres.autoHideAtEdge && !edgeCollapsed && !isDragging else {
      return
    }
    let item = DispatchWorkItem { [weak self] in
      guard let self, let panel = self.mainPanel else { return }
      let mouse = NSEvent.mouseLocation
      if !panel.frame.contains(mouse) && !self.isDragging {
        self.setEdgeCollapsed(true, animate: true)
      }
    }
    collapseWorkItem = item
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.43, execute: item)
  }

  private func cancelCollapseTimer() {
    collapseWorkItem?.cancel()
    collapseWorkItem = nil
  }

  private func setEdgeCollapsed(_ collapsed: Bool, animate: Bool) {
    guard edgeCollapsed != collapsed else { return }
    edgeCollapsed = collapsed
    hideDetail()
    applyPresentation(animate: animate)
  }

  // MARK: - 鼠标拖拽、四边吸附与连续三击归中

  private func handleMouseDown(_ event: NSEvent) {
    if edgeCollapsed {
      setEdgeCollapsed(false, animate: true)
      return
    }
    guard let panel = mainPanel else { return }

    let cursor = NSEvent.mouseLocation
    let nowTs = ProcessInfo.processInfo.systemUptime
    let withinSequence =
      (lastPressTimestamp > 0) &&
      (nowTs - lastPressTimestamp <= NSEvent.doubleClickInterval) &&
      (abs(cursor.x - lastPressLocation.x) <= 6) &&
      (abs(cursor.y - lastPressLocation.y) <= 6)

    pressCount = withinSequence ? min(pressCount + 1, 3) : 1
    lastPressTimestamp = nowTs
    lastPressLocation = cursor

    if pressCount >= 3 {
      pressCount = 0
      lastPressTimestamp = 0
      centerAlongCurrentEdge()
      return
    }

    hideDetail()
    cancelCollapseTimer()
    pointerDown = true
    isDragging = false
    dragMouseStart = cursor
    dragFrameOriginStart = panel.frame.origin
  }

  private func handleMouseDragged(_ event: NSEvent) {
    guard pointerDown, let panel = mainPanel else { return }
    let cursor = NSEvent.mouseLocation
    let dx = cursor.x - dragMouseStart.x
    let dy = cursor.y - dragMouseStart.y
    if !isDragging && max(abs(dx), abs(dy)) < 4 {
      return
    }
    isDragging = true
    pressCount = 0
    lastPressTimestamp = 0
    let newOrigin = NSPoint(x: dragFrameOriginStart.x + dx, y: dragFrameOriginStart.y + dy)
    panel.setFrameOrigin(newOrigin)
  }

  private func handleMouseUp(_ event: NSEvent) {
    guard pointerDown else { return }
    pointerDown = false
    let moved = isDragging
    isDragging = false

    guard moved, let panel = mainPanel else {
      queueEdgeCollapse()
      return
    }

    let bounds = panel.frame
    guard let screen = DisplayCatalog.screenContaining(point: NSPoint(x: bounds.midX, y: bounds.midY)) else {
      queueEdgeCollapse()
      return
    }
    let work = screen.visibleFrame

    // 计算距离可用工作区四条边的最近距离（AppKit：minY=底边，maxY=顶边）
    var targetEdge: PresentationSettings.Edge = .bottom
    var nearest = abs(bounds.minY - work.minY)

    let distTop = abs(work.maxY - bounds.maxY)
    if distTop < nearest {
      nearest = distTop
      targetEdge = .top
    }
    let distLeft = abs(bounds.minX - work.minX)
    if distLeft < nearest {
      nearest = distLeft
      targetEdge = .left
    }
    let distRight = abs(work.maxX - bounds.maxX)
    if distRight < nearest {
      targetEdge = .right
    }

    var pres = Services.shared.config.presentation
    pres.edge = targetEdge
    pres.monitorId = DisplayCatalog.screenIdentifier(screen)

    if targetEdge.isSide {
      pres.offsetX = 0
      pres.offsetY = Int((bounds.midY - work.midY).rounded())
    } else {
      pres.offsetY = 0
      pres.offsetX = Int((bounds.midX - work.midX).rounded())
    }

    Services.shared.updatePresentation(pres)
    queueEdgeCollapse()
  }

  private func centerAlongCurrentEdge() {
    var pres = Services.shared.config.presentation
    guard pres.view != .screenEdge else { return }
    pres.offsetX = 0
    pres.offsetY = 0
    Services.shared.updatePresentation(pres)
  }

  // MARK: - 布局与多显示器坐标

  private func computeLayoutRect(
    presentation: PresentationSettings,
    collapsed: Bool
  ) -> NSRect {
    guard let screen = DisplayCatalog.findScreen(id: presentation.monitorId) else {
      return NSRect(x: 0, y: 0, width: Self.barWidth, height: Self.barHeight)
    }
    let work = screen.visibleFrame
    let isRing = (presentation.view == .ring)
    let isSide = presentation.edge.isSide
    let barLength = CGFloat(presentation.effectiveBarLength)

    var width: CGFloat = barLength
    var height: CGFloat = Self.barHeight

    if isRing {
      width = Self.ringDiameter
      height = Self.ringDiameter
    } else if isSide {
      width = Self.sideBarThickness
      let avail = max(80.0, work.height - 2 * Self.snapInset)
      height = min(barLength, avail)
    }

    width = min(width, work.width)
    height = min(height, work.height)
    if isRing {
      let sq = min(width, height)
      width = sq
      height = sq
    }

    if collapsed {
      if isSide {
        width = min(width, Self.handleThickness)
        height = min(height, Self.handleLength)
      } else {
        width = min(width, Self.handleLength)
        height = min(height, Self.handleThickness)
      }
    }

    let inset: CGFloat = (presentation.autoHideAtEdge || presentation.flushWithEdge)
      ? 0.0
      : Self.snapInset

    var origin = NSPoint.zero
    if isSide {
      origin.x = (presentation.edge == .left)
        ? (work.minX + inset)
        : (work.maxX - width - inset)
      origin.y = work.midY + CGFloat(presentation.offsetY) - height / 2.0
    } else {
      origin.y = (presentation.edge == .top)
        ? (work.maxY - height - inset)
        : (work.minY + inset)
      origin.x = work.midX + CGFloat(presentation.offsetX) - width / 2.0
    }

    let clamped = clampToWorkArea(
      origin: origin,
      size: NSSize(width: width, height: height),
      work: work
    )
    return NSRect(origin: clamped, size: NSSize(width: width, height: height))
  }

  private func clampToWorkArea(origin: NSPoint, size: NSSize, work: NSRect) -> NSPoint {
    let maxX = max(work.minX, work.maxX - size.width)
    let maxY = max(work.minY, work.maxY - size.height)
    return NSPoint(
      x: min(max(origin.x, work.minX), maxX),
      y: min(max(origin.y, work.minY), maxY)
    )
  }

  // MARK: - 可见期低频刷新计时器（隐藏时停止，不驱动通知）

  private func startTimer() {
    guard refreshTimer == nil else { return }
    let timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
      Task { @MainActor in
        self?.applyState()
      }
    }
    timer.tolerance = 0.15
    refreshTimer = timer
  }

  private func stopTimer() {
    refreshTimer?.invalidate()
    refreshTimer = nil
  }
}
