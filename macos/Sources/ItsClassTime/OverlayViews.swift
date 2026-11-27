// 悬浮层视觉组件：分节长条、圆环表盘、贴边收起把手、悬停详情卡与屏幕边框绘制。
import AppKit
import ClassTimeEngine
import SwiftUI

enum WidgetPalette {
  static func kindColor(_ kind: SegmentKind) -> Color {
    switch kind {
    case .focus:
      return Color.accentColor
    case .shortBreak, .longBreak:
      return Color(nsColor: NSColor(calibratedRed: 0.247, green: 0.655, blue: 0.588, alpha: 1.0))
    case .lunch:
      return Color(nsColor: NSColor(calibratedRed: 0.878, green: 0.643, blue: 0.345, alpha: 1.0))
    }
  }

  static func kindNSColor(_ kind: SegmentKind) -> NSColor {
    switch kind {
    case .focus:
      return NSColor.controlAccentColor
    case .shortBreak, .longBreak:
      return NSColor(calibratedRed: 0.247, green: 0.655, blue: 0.588, alpha: 1.0)
    case .lunch:
      return NSColor(calibratedRed: 0.878, green: 0.643, blue: 0.345, alpha: 1.0)
    }
  }
}

// 贴边时靠边一侧采用小圆角（4pt），内侧保持胶囊圆角；关闭紧贴边缘时四角均为完整圆角。
struct DockRoundedShape: Shape {
  var radius: CGFloat
  var edge: PresentationSettings.Edge
  var flush: Bool
  var edgeRadius: CGFloat = 4

  func path(in rect: CGRect) -> Path {
    let tl: CGFloat
    let tr: CGFloat
    let br: CGFloat
    let bl: CGFloat

    if !flush {
      tl = radius
      tr = radius
      br = radius
      bl = radius
    } else {
      switch edge {
      case .top:
        tl = edgeRadius
        tr = edgeRadius
        br = radius
        bl = radius
      case .bottom:
        tl = radius
        tr = radius
        br = edgeRadius
        bl = edgeRadius
      case .left:
        tl = edgeRadius
        tr = radius
        br = radius
        bl = edgeRadius
      case .right:
        tl = radius
        tr = edgeRadius
        br = edgeRadius
        bl = radius
      }
    }

    let maxR = min(rect.width, rect.height) / 2
    let rTL = min(tl, maxR)
    let rTR = min(tr, maxR)
    let rBR = min(br, maxR)
    let rBL = min(bl, maxR)

    var p = Path()
    p.move(to: CGPoint(x: rect.minX + rTL, y: rect.minY))
    p.addLine(to: CGPoint(x: rect.maxX - rTR, y: rect.minY))
    p.addArc(
      center: CGPoint(x: rect.maxX - rTR, y: rect.minY + rTR),
      radius: rTR,
      startAngle: .degrees(-90),
      endAngle: .degrees(0),
      clockwise: false
    )
    p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - rBR))
    p.addArc(
      center: CGPoint(x: rect.maxX - rBR, y: rect.maxY - rBR),
      radius: rBR,
      startAngle: .degrees(0),
      endAngle: .degrees(90),
      clockwise: false
    )
    p.addLine(to: CGPoint(x: rect.minX + rBL, y: rect.maxY))
    p.addArc(
      center: CGPoint(x: rect.minX + rBL, y: rect.maxY - rBL),
      radius: rBL,
      startAngle: .degrees(90),
      endAngle: .degrees(180),
      clockwise: false
    )
    p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + rTL))
    p.addArc(
      center: CGPoint(x: rect.minX + rTL, y: rect.minY + rTL),
      radius: rTL,
      startAngle: .degrees(180),
      endAngle: .degrees(270),
      clockwise: false
    )
    p.closeSubpath()
    return p
  }
}

// 使用 NSTrackingArea(.activeAlways) 保证即使应用未激活（不抢焦点）也能可靠捕获鼠标悬停。
struct ActiveAlwaysHoverTracker: NSViewRepresentable {
  var onHoverChange: (Bool, CGRect) -> Void

  func makeNSView(context: Context) -> TrackingNSView {
    let view = TrackingNSView()
    view.onHoverChange = onHoverChange
    return view
  }

  func updateNSView(_ nsView: TrackingNSView, context: Context) {
    nsView.onHoverChange = onHoverChange
  }

  final class TrackingNSView: NSView {
    var onHoverChange: ((Bool, CGRect) -> Void)?
    private var trackingAreaRef: NSTrackingArea?

    override func updateTrackingAreas() {
      super.updateTrackingAreas()
      if let existing = trackingAreaRef {
        removeTrackingArea(existing)
      }
      let area = NSTrackingArea(
        rect: bounds,
        options: [.mouseEnteredAndExited, .activeAlways, .inVisibleRect],
        owner: self,
        userInfo: nil
      )
      addTrackingArea(area)
      trackingAreaRef = area
    }

    private var screenFrame: CGRect {
      guard let win = window else { return .zero }
      let rectInWindow = convert(bounds, to: nil)
      return win.convertToScreen(rectInWindow)
    }

    override func mouseEntered(with event: NSEvent) {
      onHoverChange?(true, screenFrame)
    }

    override func mouseExited(with event: NSEvent) {
      onHoverChange?(false, screenFrame)
    }
  }
}

struct WidgetSurfaceBackground: View {
  @Environment(\.colorSchemeContrast) private var contrast
  var shape: AnyShape
  var accent: Color

  var body: some View {
    if LiquidGlassCapabilities.isAvailable {
      // macOS 26+：宿主 NSPanel 已由原生 AppKit NSGlassEffectView 承载折射液态玻璃，
      // 遵循 Apple Adopting Liquid Glass 规范，禁止再叠加第二层半透明材质。
      if contrast == .increased {
        shape.stroke(Color.primary.opacity(0.50), lineWidth: 1.3)
      } else {
        Color.clear
      }
    } else if contrast == .increased {
      shape
        .fill(Color(nsColor: .windowBackgroundColor))
        .overlay(shape.stroke(Color.primary.opacity(0.45), lineWidth: 1.2))
    } else {
      shape
        .fill(.regularMaterial)
        .overlay(
          shape.fill(
            LinearGradient(
              colors: [Color.clear, accent.opacity(0.09)],
              startPoint: .topLeading,
              endPoint: .bottomTrailing
            )
          )
        )
        .overlay(shape.stroke(Color.primary.opacity(0.13), lineWidth: 1))
    }
  }
}

// MARK: - 悬浮主状态模型

@MainActor
final class OverlayStateModel: ObservableObject {
  @Published var viewMode: PresentationSettings.ViewMode = .bar
  @Published var edge: PresentationSettings.Edge = .bottom
  @Published var flushWithEdge: Bool = true
  @Published var allDay: Bool = true
  @Published var collapsed: Bool = false
  @Published var muted: Bool = false
  @Published var barBackground: String = ""
  @Published var hoveredSegmentId: String? = nil
  @Published var clock: WallClock = nowClock()
  @Published var effectiveSegments: [Segment] = []
  @Published var visibleSegments: [Segment] = []
  @Published var snapshot: Snapshot = Snapshot()
  @Published var focusCompleted: Int = 0
  @Published var focusTotal: Int = 0

  var onSegmentHover: ((String?, CGRect) -> Void)?
  var onPanelHover: ((Bool) -> Void)?

  func reload() {
    let cfg = Services.shared.config
    let now = nowClock()
    let eff = Services.shared.currentEffectiveSegments
    let state = buildWidgetState(segments: eff, now: now, scope: cfg.presentation.scope)
    viewMode = cfg.presentation.view
    edge = cfg.presentation.edge
    flushWithEdge = cfg.presentation.flushWithEdge
    allDay = cfg.presentation.scope == .allDay
    muted = cfg.notifications.muted
    barBackground = cfg.presentation.barBackground
    clock = now
    effectiveSegments = eff
    visibleSegments = state.visibleSegments
    snapshot = state.snapshot
    focusCompleted = state.focusCompleted
    focusTotal = state.focusTotal
  }

  var activeAccentColor: Color {
    if let cur = snapshot.current {
      return WidgetPalette.kindColor(cur.kind)
    }
    return .accentColor
  }
}

// MARK: - 悬浮主视图（承载分节长条 / 圆环 / 贴边收起把手）

struct OverlayRootView: View {
  @ObservedObject var model: OverlayStateModel

  var body: some View {
    Group {
      if model.collapsed {
        CollapsedHandleView(model: model)
      } else if model.viewMode == .ring {
        FloatingRingView(model: model)
      } else {
        FloatingBarView(model: model)
      }
    }
    .background(
      ActiveAlwaysHoverTracker { inside, _ in
        model.onPanelHover?(inside)
      }
    )
  }
}

// MARK: - 分节长条

struct FloatingBarView: View {
  @ObservedObject var model: OverlayStateModel

  private var isVertical: Bool { model.edge.isSide }

  var body: some View {
    let radius: CGFloat = (isVertical ? 34.0 : 32.0) / 2.0
    let dockShape = DockRoundedShape(
      radius: radius,
      edge: model.edge,
      flush: model.flushWithEdge
    )

    let customColor = WidgetPalette.color(fromHex: model.barBackground)
    let customDark = WidgetPalette.isDarkHex(model.barBackground)
    let glassRadius = model.flushWithEdge ? min(11.0, radius) : radius
    let customShape: AnyShape = LiquidGlassCapabilities.isAvailable
      ? AnyShape(RoundedRectangle(cornerRadius: glassRadius, style: .continuous))
      : AnyShape(dockShape)

    GeometryReader { geo in
      ZStack {
        if let customColor {
          customShape
            .fill(customColor)
            .overlay(
              customShape.stroke(
                (customDark ? Color.white : Color.black).opacity(0.22),
                lineWidth: 1
              )
            )
        } else {
          WidgetSurfaceBackground(shape: AnyShape(dockShape), accent: model.activeAccentColor)
        }

        if model.visibleSegments.isEmpty {
          Text(isVertical ? "无安排" : "今天暂无安排")
            .font(.system(size: isVertical ? 11 : 12, weight: .medium))
            .foregroundStyle(
              customColor != nil
                ? (customDark ? Color.white.opacity(0.65) : Color.black.opacity(0.55))
                : Color.secondary
            )
            .multilineTextAlignment(.center)
            .padding(4)
        } else {
          segmentsContent(size: geo.size)
        }
      }
    }
  }

  @ViewBuilder
  private func segmentsContent(size: CGSize) -> some View {
    let inset: CGFloat = 16
    let segments = model.visibleSegments
    let totalMinutes = max(1, segments.reduce(0) { $0 + max(1, $1.durationMinutes) })
    let contentLen = max(1.0, (isVertical ? size.height : size.width) - 2 * inset)
    let gap = min(5.0, contentLen / max(1.0, 2.0 * CGFloat(segments.count)))
    let available = max(1.0, contentLen - gap * CGFloat(max(0, segments.count - 1)))
    let minLen = min(isVertical ? 4.0 : 6.0, available / max(1.0, 2.0 * CGFloat(segments.count)))
    let weighted = max(0.0, available - minLen * CGFloat(segments.count))

    let layout = isVertical
      ? AnyLayout(VStackLayout(spacing: gap))
      : AnyLayout(HStackLayout(spacing: gap))

    layout {
      ForEach(segments) { segment in
        let length = minLen + weighted * CGFloat(max(1, segment.durationMinutes)) / CGFloat(totalMinutes)
        let progress = segmentProgress(segment)
        let isCurrent = (model.snapshot.current?.id == segment.id)
        let isHovered = (model.hoveredSegmentId == segment.id)
        SegmentBarPiece(
          segment: segment,
          length: length,
          vertical: isVertical,
          progress: progress,
          isCurrent: isCurrent,
          isHovered: isHovered,
          onHover: { inside, rect in
            model.onSegmentHover?(inside ? segment.id : nil, rect)
          }
        )
      }
    }
    .padding(isVertical ? EdgeInsets(top: inset, leading: 0, bottom: inset, trailing: 0)
                        : EdgeInsets(top: 0, leading: inset, bottom: 0, trailing: inset))
  }

  private func segmentProgress(_ segment: Segment) -> Double {
    widgetSegmentProgress(segment: segment, now: model.clock, snapshot: model.snapshot)
  }
}

private struct SegmentBarPiece: View {
  let segment: Segment
  let length: CGFloat
  let vertical: Bool
  let progress: Double
  let isCurrent: Bool
  let isHovered: Bool
  let onHover: (Bool, CGRect) -> Void

  var body: some View {
    let baseThickness: CGFloat = (segment.kind == .shortBreak) ? 5.0 : 8.0
    let thickness: CGFloat = isHovered ? (baseThickness + 1.5) : baseThickness
    let radius = min(length, thickness) / 2.0
    let color = WidgetPalette.kindColor(segment.kind)
    let filled = max(0.0, min(length, length * CGFloat(progress)))

    ZStack(alignment: vertical ? .top : .leading) {
      RoundedRectangle(cornerRadius: radius, style: .continuous)
        .fill(color.opacity(LiquidGlassCapabilities.isAvailable ? 0.24 : 0.20))
        .overlay(
          RoundedRectangle(cornerRadius: radius, style: .continuous)
            .stroke(
              isHovered ? color.opacity(0.65) : (isCurrent ? color.opacity(0.40) : Color.primary.opacity(0.08)),
              lineWidth: isHovered ? 1.1 : 0.6
            )
        )
        .frame(
          width: vertical ? thickness : length,
          height: vertical ? length : thickness
        )

      if filled > 0.2 {
        RoundedRectangle(cornerRadius: min(filled, thickness) / 2.0, style: .continuous)
          .fill(color)
          .frame(
            width: vertical ? thickness : filled,
            height: vertical ? filled : thickness
          )
      }
    }
    .frame(
      width: vertical ? 24.0 : length,
      height: vertical ? length : 24.0
    )
    .contentShape(Rectangle())
    .background(
      ActiveAlwaysHoverTracker { inside, rect in
        onHover(inside, rect)
      }
    )
  }
}

// MARK: - 圆环表盘

struct FloatingRingView: View {
  @ObservedObject var model: OverlayStateModel

  var body: some View {
    let readout = computeReadout()
    let progress = (model.snapshot.current != nil) ? model.snapshot.progress : 0.0
    let ringColor = model.activeAccentColor

    ZStack {
      WidgetSurfaceBackground(shape: AnyShape(Circle()), accent: ringColor)

      if !LiquidGlassCapabilities.isAvailable {
        Circle()
          .inset(by: 3.5)
          .stroke(Color.primary.opacity(0.10), lineWidth: 1)
      }

      Circle()
        .inset(by: 13)
        .stroke(Color.primary.opacity(LiquidGlassCapabilities.isAvailable ? 0.14 : 0.11), lineWidth: 6.5)

      if progress > 0.001 {
        Circle()
          .inset(by: 13)
          .trim(from: 0, to: CGFloat(min(1.0, max(0.0, progress))))
          .stroke(
            ringColor,
            style: StrokeStyle(lineWidth: 6.5, lineCap: .round)
          )
          .rotationEffect(.degrees(-90))
      }

      VStack(spacing: 2) {
        if !readout.label.isEmpty {
          Text(readout.label)
            .font(.system(size: 12, weight: .medium))
            .foregroundStyle(.secondary)
            .lineLimit(1)
        }

        Text(readout.time)
          .font(
            .system(
              size: readout.label.isEmpty ? 17 : (readout.time.count > 5 ? 25 : 30),
              weight: .semibold,
              design: .rounded
            )
          )
          .monospacedDigit()
          .foregroundStyle(.primary)
          .shadow(color: Color.black.opacity(LiquidGlassCapabilities.isAvailable ? 0.10 : 0.0), radius: 1.5, y: 0.5)
          .lineLimit(1)

        if !readout.summary.isEmpty {
          Text(readout.summary)
            .font(.system(size: 11))
            .foregroundStyle(.secondary)
            .lineLimit(1)
            .padding(.top, 2)
        } else if model.muted {
          Image(systemName: "bell.slash.fill")
            .font(.system(size: 10))
            .foregroundStyle(.secondary)
            .padding(.top, 2)
        }
      }
      .padding(.horizontal, 18)
    }
    .background(
      ActiveAlwaysHoverTracker { inside, rect in
        model.onSegmentHover?(inside ? readout.detailSegmentId : nil, rect)
      }
    )
  }

  private struct RingReadout {
    var label: String = ""
    var time: String = ""
    var summary: String = ""
    var detailSegmentId: String?
  }

  private func computeReadout() -> RingReadout {
    var result = RingReadout()
    let now = model.clock
    let nowMinutes = now.minutesSinceMidnight
    let plan = model.effectiveSegments
    let snap = model.snapshot

    if let current = snap.current {
      result.label = (current.kind == .focus)
        ? "第 \(current.focusOrdinal) 节剩余"
        : "\(Formatters.segmentKindLabel(current.kind))剩余"
      let remaining = (current.end.totalMinutes - nowMinutes) * 60 - now.second
      result.time = Formatters.remainingClock(remaining)
      result.detailSegmentId = current.id
    } else if let next = snap.next {
      let started = plan.contains { $0.start.totalMinutes <= nowMinutes }
      result.label = started ? "空档 · 下一段" : "首段开始"
      result.time = Formatters.clock(next.start)
      result.detailSegmentId = next.id
    } else {
      result.time = snap.hasSchedule ? "今日已结束" : "今天没有安排"
    }

    if model.allDay, model.focusTotal > 0 {
      result.summary = "今日 \(model.focusCompleted) / \(model.focusTotal) 节"
    }
    return result
  }
}

// MARK: - 贴边自动收起进度把手

struct CollapsedHandleView: View {
  @ObservedObject var model: OverlayStateModel

  var body: some View {
    let isSide = model.edge.isSide
    let trackLength: CGFloat = 64
    let trackThickness: CGFloat = 4
    let progress = (model.snapshot.current != nil) ? model.snapshot.progress : 0.0
    let filled = trackLength * CGFloat(max(0.0, min(1.0, progress)))
    let fillColor = model.activeAccentColor
    let shape = DockRoundedShape(
      radius: 6,
      edge: model.edge,
      flush: true,
      edgeRadius: 2
    )

    ZStack {
      WidgetSurfaceBackground(shape: AnyShape(shape), accent: fillColor)

      ZStack(alignment: isSide ? .top : .leading) {
        Capsule()
          .fill(Color.primary.opacity(0.22))
          .frame(
            width: isSide ? trackThickness : trackLength,
            height: isSide ? trackLength : trackThickness
          )

        if filled > 0.2 {
          Capsule()
            .fill(fillColor)
            .frame(
              width: isSide ? trackThickness : filled,
              height: isSide ? filled : trackThickness
            )
        }
      }
    }
  }
}

// MARK: - 悬停详情卡片

struct HoverDetailCardData {
  var title: String
  var rangeText: String
  var percentText: String
  var progress: Double
  var timingText: String
  var nextText: String
  var muted: Bool
  var kind: SegmentKind
}

@MainActor
final class HoverDetailModel: ObservableObject {
  @Published var data: HoverDetailCardData?
}

struct HoverDetailCardView: View {
  @ObservedObject var model: HoverDetailModel

  var body: some View {
    if let data = model.data {
      let accent = WidgetPalette.kindColor(data.kind)
      let cardShape = RoundedRectangle(cornerRadius: 18, style: .continuous)

      VStack(alignment: .leading, spacing: 7) {
        HStack(spacing: 8) {
          RoundedRectangle(cornerRadius: 2, style: .continuous)
            .fill(accent)
            .frame(width: 4, height: 14)

          Text(data.title)
            .font(.system(size: 14.5, weight: .semibold))
            .foregroundStyle(.primary)
            .lineLimit(1)
        }

        Text(data.rangeText)
          .font(.system(size: 12))
          .foregroundStyle(.secondary)
          .lineLimit(1)

        HStack(spacing: 8) {
          GeometryReader { geo in
            ZStack(alignment: .leading) {
              Capsule()
                .fill(Color.primary.opacity(0.13))
              Capsule()
                .fill(accent)
                .frame(width: geo.size.width * CGFloat(max(0.0, min(1.0, data.progress))))
            }
          }
          .frame(height: 7)

          Text(data.percentText)
            .font(.system(size: 12.5, weight: .semibold))
            .monospacedDigit()
            .foregroundStyle(.primary)
            .frame(minWidth: 38, alignment: .trailing)
        }

        Text(data.timingText)
          .font(.system(size: 12))
          .monospacedDigit()
          .foregroundStyle(.secondary)
          .lineLimit(1)

        Text(data.nextText)
          .font(.system(size: 12))
          .foregroundStyle(.secondary)
          .lineLimit(1)

        if data.muted {
          Label("提醒已暂停（时间进度照常前进）", systemImage: "bell.slash.fill")
            .font(.system(size: 11.5, weight: .medium))
            .foregroundStyle(.secondary)
            .padding(.top, 1)
        }
      }
      .padding(.horizontal, 14)
      .padding(.vertical, 12)
      .frame(width: 280, alignment: .leading)
      .background(WidgetSurfaceBackground(shape: AnyShape(cardShape), accent: accent))
    }
  }
}

// MARK: - 屏幕四边顺时针进度环绘制视图（支持“按时段分色”与“跟随当前时段”两种配色）

struct ScreenEdgePerimeterView: View {
  var segments: [Segment] = []
  var now: WallClock = nowClock()
  var scope: PresentationSettings.ScopeMode = .allDay
  var colorMode: PresentationSettings.ScreenEdgeColors = .schedule
  var progress: Double
  var thickness: CGFloat
  var color: Color

  var body: some View {
    Canvas { context, size in
      let w = size.width
      let h = size.height
      let t = min(thickness, min(w, h) / 3.0)
      guard w > 4 && h > 4 && t >= 1 else { return }

      // 全天分色模式：沿四边顺时针按真实时段时长分色，并在相邻交界处平滑过渡。
      if scope == .allDay && colorMode == .schedule {
        let runs = buildScreenEdgeRuns(segments: segments, now: now)
        if !runs.isEmpty {
          drawScheduleBorder(context: &context, width: w, height: h, thickness: t, runs: runs)
          return
        }
      }

      // 单色进度模式（当前时段 / 跟随当前时段 / 空日程底轨）
      let trackColor = Color(.sRGB, red: 100.0 / 255.0, green: 117.0 / 255.0, blue: 137.0 / 255.0, opacity: 172.0 / 255.0)
      var outer = Path()
      outer.addRect(CGRect(x: 0, y: 0, width: w, height: h))
      outer.addRect(CGRect(x: t, y: t, width: w - 2 * t, height: h - 2 * t))
      context.fill(outer, with: .color(trackColor), style: FillStyle(eoFill: true))

      let clamped = max(0.0, min(1.0, progress))
      guard clamped > 0.0005 else { return }

      let half = t / 2.0
      let minX = half
      let maxX = w - half
      let minY = half
      let maxY = h - half
      let topLen = max(0, maxX - minX)
      let rightLen = max(0, maxY - minY)
      let bottomLen = topLen
      let leftLen = rightLen
      let perimeter = topLen + rightLen + bottomLen + leftLen
      guard perimeter > 0 else { return }

      var remaining = perimeter * CGFloat(clamped)
      var progressPath = Path()
      progressPath.move(to: CGPoint(x: minX, y: minY))

      let segTop = min(remaining, topLen)
      progressPath.addLine(to: CGPoint(x: minX + segTop, y: minY))
      remaining -= segTop

      if remaining > 0 {
        let segRight = min(remaining, rightLen)
        progressPath.addLine(to: CGPoint(x: maxX, y: minY + segRight))
        remaining -= segRight
      }

      if remaining > 0 {
        let segBottom = min(remaining, bottomLen)
        progressPath.addLine(to: CGPoint(x: maxX - segBottom, y: maxY))
        remaining -= segBottom
      }

      if remaining > 0 {
        let segLeft = min(remaining, leftLen)
        progressPath.addLine(to: CGPoint(x: minX, y: maxY - segLeft))
      }

      context.stroke(
        progressPath,
        with: .color(color),
        style: StrokeStyle(lineWidth: t, lineCap: .round, lineJoin: .miter)
      )
    }
    .allowsHitTesting(false)
  }

  private func drawScheduleBorder(
    context: inout GraphicsContext,
    width w: CGFloat,
    height h: CGFloat,
    thickness t: CGFloat,
    runs: [ScreenEdgeRun]
  ) {
    let lengths: [Double] = [
      Double(w),
      Double(max(0, h - t)),
      Double(max(0, w - t)),
      Double(max(0, h - 2 * t)),
    ]
    let perimeter = lengths.reduce(0.0, +)
    guard perimeter > 0 else { return }

    let stops = WidgetPalette.screenEdgeColorStops(runs: runs)
    guard !stops.isEmpty else { return }

    // 四条互不重叠的边：左上角出发顺时针（上、右、下、左），避免半透明转角双重叠加。
    let rects: [CGRect] = [
      CGRect(x: 0, y: 0, width: w, height: t),
      CGRect(x: w - t, y: t, width: t, height: max(0, h - t)),
      CGRect(x: 0, y: h - t, width: max(0, w - t), height: t),
      CGRect(x: 0, y: t, width: t, height: max(0, h - 2 * t)),
    ]

    var sideStart = 0.0
    for side in 0..<4 {
      let sideLen = lengths[side]
      let rect = rects[side]
      guard sideLen > 0 && rect.width > 0 && rect.height > 0 else {
        sideStart += sideLen
        continue
      }

      let begin = sideStart / perimeter
      let end = (sideStart + sideLen) / perimeter
      let span = end - begin

      var gradStops: [Gradient.Stop] = [
        Gradient.Stop(color: WidgetPalette.screenEdgeColorAt(stops: stops, position: begin), location: 0.0)
      ]
      for stop in stops where stop.position > begin && stop.position < end {
        let loc = (stop.position - begin) / span
        gradStops.append(Gradient.Stop(color: stop.rgba.swiftUIColor, location: loc))
      }
      gradStops.append(
        Gradient.Stop(color: WidgetPalette.screenEdgeColorAt(stops: stops, position: end), location: 1.0)
      )

      let startPoint: CGPoint
      let endPoint: CGPoint
      switch side {
      case 0:
        startPoint = CGPoint(x: rect.minX, y: rect.midY)
        endPoint = CGPoint(x: rect.maxX, y: rect.midY)
      case 1:
        startPoint = CGPoint(x: rect.midX, y: rect.minY)
        endPoint = CGPoint(x: rect.midX, y: rect.maxY)
      case 2:
        startPoint = CGPoint(x: rect.maxX, y: rect.midY)
        endPoint = CGPoint(x: rect.minX, y: rect.midY)
      default:
        startPoint = CGPoint(x: rect.midX, y: rect.maxY)
        endPoint = CGPoint(x: rect.midX, y: rect.minY)
      }

      context.fill(
        Path(rect),
        with: .linearGradient(Gradient(stops: gradStops), startPoint: startPoint, endPoint: endPoint)
      )
      sideStart += sideLen
    }
  }
}
