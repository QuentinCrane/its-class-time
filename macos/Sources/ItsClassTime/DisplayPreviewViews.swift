// 「显示与位置」实时预览与显示形式选择器：
// - 与真实作息引擎、当前时钟、配色、长条长度/背景色、边框粗细/分色、贴边位置和自动隐藏完全同步。
// - 包含左侧“组件细节近景”与右侧“缩放桌面位置示意”，预览计时器仅在设置窗口可见且显示页激活时运行。
import AppKit
import ClassTimeEngine
import SwiftUI

struct ViewModeChoiceGrid: View {
  let selected: PresentationSettings.ViewMode
  let clearGlass: Bool
  let onSelect: (PresentationSettings.ViewMode) -> Void

  var body: some View {
    ViewThatFits(in: .horizontal) {
      HStack(spacing: 10) {
        choiceCards
      }
      VStack(spacing: 8) {
        choiceCards
      }
    }
  }

  @ViewBuilder
  private var choiceCards: some View {
    choiceButton(mode: .bar, title: "分节长条")
    choiceButton(mode: .ring, title: "圆环")
    choiceButton(mode: .screenEdge, title: "屏幕边框")
  }

  @ViewBuilder
  private func choiceButton(mode: PresentationSettings.ViewMode, title: String) -> some View {
    let active = (selected == mode)
    Button {
      onSelect(mode)
    } label: {
      VStack(spacing: 8) {
        miniGraphic(mode: mode, active: active)
          .frame(width: 96, height: 32)
        Text(title)
          .font(.system(size: 13.5, weight: .semibold))
          .foregroundStyle(active ? Color.accentColor : Color.primary)
      }
      .frame(maxWidth: .infinity)
      .padding(.vertical, 10)
      .padding(.horizontal, 8)
      .background(
        RoundedRectangle(cornerRadius: 11, style: .continuous)
          .fill(active ? Color.accentColor.opacity(0.14) : Color.primary.opacity(0.04))
          .overlay(
            RoundedRectangle(cornerRadius: 11, style: .continuous)
              .stroke(
                active ? Color.accentColor : Color.primary.opacity(0.12),
                lineWidth: active ? 1.5 : 1.0
              )
          )
      )
    }
    .buttonStyle(.plain)
    .accessibilityLabel(title)
    .accessibilityAddTraits(active ? .isSelected : [])
  }

  @ViewBuilder
  private func miniGraphic(mode: PresentationSettings.ViewMode, active: Bool) -> some View {
    let tint = active ? Color.accentColor : Color.secondary
    switch mode {
    case .bar:
      HStack(spacing: 4) {
        ForEach(0..<5, id: \.self) { idx in
          Capsule()
            .fill(tint.opacity(idx < 3 ? 1.0 : 0.32))
            .frame(width: 14, height: 7)
        }
      }
    case .ring:
      ZStack {
        Circle()
          .stroke(tint.opacity(0.28), lineWidth: 4.5)
        Circle()
          .trim(from: 0, to: 0.68)
          .stroke(tint, style: StrokeStyle(lineWidth: 4.5, lineCap: .round))
          .rotationEffect(.degrees(-90))
      }
      .frame(width: 26, height: 26)
    case .screenEdge:
      RoundedRectangle(cornerRadius: 4, style: .continuous)
        .stroke(tint, lineWidth: 3)
        .frame(width: 62, height: 26)
    }
  }
}

struct DisplayLivePreviewCard: View {
  let presentation: PresentationSettings
  let segments: [Segment]
  let now: WallClock
  let hovered: Bool
  let clearGlass: Bool
  let onHoverChange: (Bool) -> Void

  var body: some View {
    let state = buildWidgetState(segments: segments, now: now, scope: presentation.scope)
    let activeColor: Color = if let cur = state.snapshot.current {
      WidgetPalette.kindColor(cur.kind)
    } else {
      .accentColor
    }

    VStack(alignment: .leading, spacing: 8) {
      HStack {
        Text("实时预览")
          .font(.system(size: 15, weight: .semibold))
        Spacer()
        Text(liveSummaryText(state: state))
          .font(.system(size: 12))
          .monospacedDigit()
          .foregroundStyle(.secondary)
          .lineLimit(1)
      }

      HStack(spacing: 12) {
        // 左侧：组件细节近景（与真实有效作息和外观设置同步）
        LiveComponentDetailPreview(
          presentation: presentation,
          segments: segments,
          state: state,
          now: now,
          hovered: hovered,
          accent: activeColor
        )
        .frame(maxWidth: .infinity)
        .frame(height: 112)
        .background(
          RoundedRectangle(cornerRadius: 10, style: .continuous)
            .fill(Color.primary.opacity(0.055))
            .overlay(
              RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(Color.primary.opacity(0.09), lineWidth: 0.7)
            )
        )

        // 右侧：缩放 macOS 桌面位置示意
        LiveDesktopPositionPreview(
          presentation: presentation,
          segments: segments,
          state: state,
          now: now,
          hovered: hovered,
          accent: activeColor
        )
        .frame(width: 214, height: 112)
      }
    }
    .padding(.horizontal, 16)
    .padding(.vertical, 12)
    .frame(maxWidth: .infinity, alignment: .leading)
    .adaptiveGlassCard(cornerRadius: LiquidGlassCapabilities.isAvailable ? 16 : 12, clearStyle: clearGlass)
    .help("位置为缩放示意；自动隐藏时悬停查看展开效果。")
    .background(
      ActiveAlwaysHoverTracker { inside, _ in
        onHoverChange(inside)
      }
    )
  }

  private func liveSummaryText(state: WidgetState) -> String {
    let snap = state.snapshot
    if let cur = snap.current {
      let total = cur.durationMinutes
      let elapsed = Int((Double(total) * snap.progress).rounded())
      let name = (cur.kind == .focus && cur.focusOrdinal > 0)
        ? "第 \(cur.focusOrdinal) 节"
        : Formatters.segmentKindLabel(cur.kind)
      return "当前：\(name) · 已过 \(Formatters.percent(snap.progress))（\(Formatters.minutes(elapsed)) / \(Formatters.minutes(total))）"
    }
    if let nxt = snap.next {
      return "未在时段内 · 下一个 \(nxt.label) \(Formatters.clock(nxt.start))–\(Formatters.clock(nxt.end))"
    }
    return snap.hasSchedule ? "今日计划已结束。" : "今天没有安排的时段。"
  }
}

// MARK: - 左侧近景预览

private struct LiveComponentDetailPreview: View {
  let presentation: PresentationSettings
  let segments: [Segment]
  let state: WidgetState
  let now: WallClock
  let hovered: Bool
  let accent: Color

  var body: some View {
    let side = presentation.edge.isSide
    let expanded = hovered && presentation.autoHideAtEdge

    ZStack {
      if !presentation.visible {
        Text("桌面组件已隐藏")
          .font(.system(size: 13.5, weight: .medium))
          .foregroundStyle(.secondary)
      } else if presentation.view != .screenEdge && presentation.autoHideAtEdge && !expanded {
        // 收起把手近景
        let progress = state.snapshot.current != nil ? state.snapshot.progress : 0.0
        ZStack {
          RoundedRectangle(cornerRadius: 6, style: .continuous)
            .fill(Color(nsColor: .controlBackgroundColor))
            .overlay(
              RoundedRectangle(cornerRadius: 6, style: .continuous)
                .stroke(Color.primary.opacity(0.14), lineWidth: 1)
            )
            .frame(width: side ? 12 : 84, height: side ? 84 : 12)

          ZStack(alignment: side ? .top : .leading) {
            Capsule()
              .fill(Color.primary.opacity(0.16))
              .frame(width: side ? 4 : 64, height: side ? 64 : 4)
            if progress > 0.005 {
              Capsule()
                .fill(accent)
                .frame(
                  width: side ? 4 : max(4, 64 * CGFloat(progress)),
                  height: side ? max(4, 64 * CGFloat(progress)) : 4
                )
            }
          }
        }
      } else if presentation.view == .screenEdge {
        ScreenEdgePerimeterView(
          segments: segments,
          now: now,
          scope: presentation.scope,
          colorMode: presentation.screenEdgeColors,
          progress: state.edgeProgress,
          thickness: max(2.5, min(10.0, CGFloat(presentation.screenEdgeThickness) * 0.45)),
          color: accent
        )
        .padding(.horizontal, 28)
        .padding(.vertical, 12)
      } else if presentation.view == .bar {
        barDetailView(side: side)
      } else {
        ringDetailView()
      }
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .clipped()
  }

  @ViewBuilder
  private func barDetailView(side: Bool) -> some View {
    let fraction = min(1.0, max(0.0, Double(presentation.effectiveBarLength - 240) / 1360.0))
    let barLength: CGFloat = side ? CGFloat(48 + 48 * fraction) : CGFloat(126 + 210 * fraction)
    let barWidth: CGFloat = side ? 30 : barLength
    let barHeight: CGFloat = side ? barLength : 28
    let shape = DockRoundedShape(
      radius: min(barWidth, barHeight) / 2.0,
      edge: presentation.edge,
      flush: presentation.flushWithEdge
    )
    let customBg = WidgetPalette.color(fromHex: presentation.barBackground)
    let customDark = WidgetPalette.isDarkHex(presentation.barBackground)

    ZStack {
      if let customBg {
        shape
          .fill(customBg)
          .overlay(shape.stroke((customDark ? Color.white : Color.black).opacity(0.24), lineWidth: 1))
      } else {
        shape
          .fill(Color(nsColor: .controlBackgroundColor).opacity(0.85))
          .overlay(shape.stroke(Color.primary.opacity(0.16), lineWidth: 1))
      }

      if state.visibleSegments.isEmpty {
        Text(side ? "无" : "今天暂无安排")
          .font(.system(size: side ? 9 : 11, weight: .medium))
          .foregroundStyle(
            customBg != nil
              ? (customDark ? Color.white.opacity(0.65) : Color.black.opacity(0.55))
              : Color.secondary
          )
      } else {
        let total = max(1, state.visibleSegments.reduce(0) { $0 + max(1, $1.durationMinutes) })
        let inset: CGFloat = 12
        let inner = max(12.0, barLength - 2 * inset)
        let count = CGFloat(state.visibleSegments.count)
        let gap = min(4.0, inner / (2.0 * count))
        let avail = max(1.0, inner - gap * max(0, count - 1))
        let minSpan = min(side ? 3.5 : 5.0, avail / (2.0 * count))
        let weighted = max(0.0, avail - minSpan * count)

        let layout = side
          ? AnyLayout(VStackLayout(spacing: gap))
          : AnyLayout(HStackLayout(spacing: gap))

        layout {
          ForEach(state.visibleSegments) { seg in
            let span = minSpan + weighted * CGFloat(max(1, seg.durationMinutes)) / CGFloat(total)
            let line: CGFloat = (seg.kind == .shortBreak) ? 4.5 : 7.0
            let kindColor = WidgetPalette.kindColor(seg.kind)
            let progress = widgetSegmentProgress(segment: seg, now: now, snapshot: state.snapshot)

            ZStack(alignment: side ? .top : .leading) {
              Capsule()
                .fill(kindColor.opacity(0.22))
                .frame(width: side ? line : span, height: side ? span : line)
              if progress > 0.005 {
                Capsule()
                  .fill(kindColor)
                  .frame(
                    width: side ? line : max(line * 0.5, span * CGFloat(progress)),
                    height: side ? max(line * 0.5, span * CGFloat(progress)) : line
                  )
              }
            }
          }
        }
      }
    }
    .frame(width: barWidth, height: barHeight)
  }

  @ViewBuilder
  private func ringDetailView() -> some View {
    let progress = state.snapshot.current != nil ? state.snapshot.progress : 0.0
    let readout = computeRingReadout()

    ZStack {
      Circle()
        .fill(Color(nsColor: .controlBackgroundColor).opacity(0.9))
        .overlay(Circle().stroke(Color.primary.opacity(0.14), lineWidth: 1))
      Circle()
        .inset(by: 7)
        .stroke(Color.primary.opacity(0.12), lineWidth: 4.5)
      if progress > 0.002 {
        Circle()
          .inset(by: 7)
          .trim(from: 0, to: CGFloat(min(1.0, max(0.0, progress))))
          .stroke(accent, style: StrokeStyle(lineWidth: 4.5, lineCap: .round))
          .rotationEffect(.degrees(-90))
      }
      VStack(spacing: 1) {
        if !readout.label.isEmpty {
          Text(readout.label)
            .font(.system(size: 8.5, weight: .medium))
            .foregroundStyle(.secondary)
            .lineLimit(1)
        }
        Text(readout.value)
          .font(.system(size: readout.label.isEmpty ? 11.5 : 18, weight: .semibold, design: .rounded))
          .monospacedDigit()
          .foregroundStyle(.primary)
          .lineLimit(1)
        if presentation.scope == .allDay && state.focusTotal > 0 {
          Text("今日 \(state.focusCompleted) / \(state.focusTotal) 节")
            .font(.system(size: 8))
            .foregroundStyle(.secondary)
        }
      }
      .padding(.horizontal, 10)
    }
    .frame(width: 96, height: 96)
  }

  private func computeRingReadout() -> (label: String, value: String) {
    if let cur = state.snapshot.current {
      let lbl = (cur.kind == .focus)
        ? "第 \(cur.focusOrdinal) 节剩余"
        : "\(Formatters.segmentKindLabel(cur.kind))剩余"
      let rem = (cur.end.totalMinutes - now.minutesSinceMidnight) * 60 - now.second
      return (lbl, Formatters.remainingClock(rem))
    }
    if let nxt = state.snapshot.next {
      let started = segments.contains { $0.start.totalMinutes <= now.minutesSinceMidnight }
      return (started ? "空档 · 下一段" : "首段开始", Formatters.clock(nxt.start))
    }
    return ("", state.snapshot.hasSchedule ? "今日已结束" : "今天没有安排")
  }
}

// MARK: - 右侧缩放桌面位置示意

private struct LiveDesktopPositionPreview: View {
  let presentation: PresentationSettings
  let segments: [Segment]
  let state: WidgetState
  let now: WallClock
  let hovered: Bool
  let accent: Color

  private var menuBarMockLabel: String {
    switch presentation.menuBarDisplay {
    case .iconOnly: return ""
    case .countdown:
      if let cur = state.snapshot.current {
        let rem = (cur.end.totalMinutes - now.minutesSinceMidnight) * 60 - now.second
        return Formatters.remainingClock(rem)
      }
      return state.snapshot.next.map { Formatters.clock($0.start) } ?? ""
    case .detailed:
      if let cur = state.snapshot.current {
        let rem = (cur.end.totalMinutes - now.minutesSinceMidnight) * 60 - now.second
        let prefix = (cur.kind == .focus) ? "第\(cur.focusOrdinal)节" : Formatters.segmentKindLabel(cur.kind)
        return "\(prefix) \(Formatters.remainingClock(rem))"
      }
      return state.snapshot.next.map { "下段 \(Formatters.clock($0.start))" } ?? ""
    }
  }

  var body: some View {
    ZStack {
      RoundedRectangle(cornerRadius: 10, style: .continuous)
        .fill(
          LinearGradient(
            colors: [
              Color(red: 0.11, green: 0.17, blue: 0.27),
              Color(red: 0.16, green: 0.25, blue: 0.39),
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
          )
        )

      VStack(spacing: 0) {
        // 顶部 macOS 菜单栏示意
        HStack(spacing: 3) {
          Circle().fill(Color.white.opacity(0.55)).frame(width: 3.5, height: 3.5)
          Spacer()
          HStack(spacing: 2.5) {
            ZStack {
              Circle().stroke(Color.white.opacity(0.35), lineWidth: 1.0)
              Circle()
                .trim(from: 0, to: CGFloat(state.snapshot.current != nil ? max(0.08, state.snapshot.progress) : 0.6))
                .stroke(Color.white, style: StrokeStyle(lineWidth: 1.2, lineCap: .round))
                .rotationEffect(.degrees(-90))
            }
            .frame(width: 6.5, height: 6.5)
            if !menuBarMockLabel.isEmpty {
              Text(menuBarMockLabel)
                .font(.system(size: 6.8, weight: .semibold, design: .rounded))
                .monospacedDigit()
                .foregroundStyle(Color.white.opacity(0.92))
            }
          }
          .padding(.horizontal, 4)
          .padding(.vertical, 1)
          .background(Capsule().fill(Color.white.opacity(0.16)))
        }
        .padding(.horizontal, 6)
        .frame(height: 13)
        .background(Color.black.opacity(0.26))

        // 工作区（按真实偏移与尺寸缩放）
        GeometryReader { geo in
          ZStack {
            LinearGradient(
              colors: [
                Color(red: 0.15, green: 0.23, blue: 0.37),
                Color(red: 0.20, green: 0.30, blue: 0.44),
              ],
              startPoint: .topLeading,
              endPoint: .bottomTrailing
            )

            if !presentation.visible {
              Text("已隐藏")
                .font(.system(size: 10, weight: .medium))
                .foregroundStyle(Color.white.opacity(0.75))
            } else if presentation.view != .screenEdge {
              positionedOverlay(in: geo.size)
            }
          }
        }

        // 底部 Dock 示意
        HStack(spacing: 3) {
          ForEach(0..<4, id: \.self) { _ in
            RoundedRectangle(cornerRadius: 1.5).fill(Color.white.opacity(0.55)).frame(width: 6, height: 4.5)
          }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 2)
        .background(Capsule().fill(Color.white.opacity(0.18)))
        .padding(.vertical, 2)
        .frame(maxWidth: .infinity)
        .background(Color.black.opacity(0.16))
      }
      .overlay {
        if presentation.visible && presentation.view == .screenEdge {
          ScreenEdgePerimeterView(
            segments: segments,
            now: now,
            scope: presentation.scope,
            colorMode: presentation.screenEdgeColors,
            progress: state.edgeProgress,
            thickness: max(2.0, CGFloat(presentation.screenEdgeThickness) * 0.35),
            color: accent
          )
        }
      }
      .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
      .padding(4)
    }
  }

  @ViewBuilder
  private func positionedOverlay(in size: CGSize) -> some View {
    let side = presentation.edge.isSide
    let collapsed = presentation.autoHideAtEdge && !hovered
    let inset: CGFloat = (presentation.flushWithEdge || collapsed) ? 1 : 5
    let fraction = min(1.0, max(0.0, Double(presentation.effectiveBarLength - 240) / 1360.0))

    let widgetSize: CGSize = {
      if collapsed {
        return side ? CGSize(width: 5, height: 28) : CGSize(width: 28, height: 5)
      }
      if presentation.view == .ring {
        return CGSize(width: 32, height: 32)
      }
      let len = side ? CGFloat(28 + 38 * fraction) : CGFloat(54 + 104 * fraction)
      return side ? CGSize(width: 11, height: min(len, size.height - 2 * inset))
                  : CGSize(width: min(len, size.width - 2 * inset), height: 11)
    }()

    let center: CGPoint = {
      let halfW = widgetSize.width / 2
      let halfH = widgetSize.height / 2
      if side {
        let x = (presentation.edge == .left) ? (inset + halfW) : (size.width - inset - halfW)
        let mappedY = size.height / 2 - CGFloat(presentation.offsetY) * size.height / 900.0
        let y = min(max(mappedY, inset + halfH), max(inset + halfH, size.height - inset - halfH))
        return CGPoint(x: x, y: y)
      } else {
        let y = (presentation.edge == .top) ? (inset + halfH) : (size.height - inset - halfH)
        let mappedX = size.width / 2 + CGFloat(presentation.offsetX) * size.width / 1440.0
        let x = min(max(mappedX, inset + halfW), max(inset + halfW, size.width - inset - halfW))
        return CGPoint(x: x, y: y)
      }
    }()

    let barBg = WidgetPalette.color(fromHex: presentation.barBackground)

    Group {
      if collapsed {
        Capsule()
          .fill(accent)
          .overlay(Capsule().stroke(Color.white.opacity(0.45), lineWidth: 0.5))
      } else if presentation.view == .ring {
        ZStack {
          Circle().fill(Color.white.opacity(0.26))
          Circle().inset(by: 3).stroke(Color.white.opacity(0.22), lineWidth: 2.2)
          Circle()
            .inset(by: 3)
            .trim(from: 0, to: CGFloat(state.snapshot.current != nil ? max(0.06, state.snapshot.progress) : 0))
            .stroke(accent, style: StrokeStyle(lineWidth: 2.2, lineCap: .round))
            .rotationEffect(.degrees(-90))
        }
      } else {
        let shape = DockRoundedShape(
          radius: min(widgetSize.width, widgetSize.height) / 2,
          edge: presentation.edge,
          flush: presentation.flushWithEdge,
          edgeRadius: 2
        )
        ZStack {
          shape
            .fill(barBg ?? Color.white.opacity(0.28))
            .overlay(shape.stroke(Color.white.opacity(0.45), lineWidth: 0.6))
          if side {
            VStack(spacing: 1.5) {
              ForEach(state.visibleSegments.prefix(6)) { seg in
                Capsule().fill(WidgetPalette.kindColor(seg.kind)).frame(width: 3, height: 4)
              }
            }
          } else {
            HStack(spacing: 1.5) {
              ForEach(state.visibleSegments.prefix(8)) { seg in
                Capsule().fill(WidgetPalette.kindColor(seg.kind)).frame(width: 6, height: 3)
              }
            }
          }
        }
      }
    }
    .frame(width: widgetSize.width, height: widgetSize.height)
    .position(center)
  }
}
