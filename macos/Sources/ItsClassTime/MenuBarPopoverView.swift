// 顶部状态栏展开面板视图（遵循 macOS 26/27 Liquid Glass Menu Bar Extra 规范）：
// 展示实时进度环、剩余时间、今日紧凑时间线及悬浮窗/形态/范围/状态栏/暂停提醒快捷控制。
// 兼容 Command Line Tools 构建：仅使用 ObservableObject + @ObservedObject。
import AppKit
import ClassTimeEngine
import SwiftUI

@MainActor
final class MenuBarPanelModel: ObservableObject {
  @Published var clock: WallClock = nowClock()
  @Published var snapshot: Snapshot = Snapshot()
  @Published var effectiveSegments: [Segment] = []
  @Published var presentation: PresentationSettings = PresentationSettings()
  @Published var notifications: NotificationSettings = NotificationSettings()
  @Published var templateName: String = ""
  @Published var usingOverride: Bool = false

  func reload() {
    let services = Services.shared
    let cfg = services.config
    clock = nowClock()
    snapshot = services.snapshot
    effectiveSegments = sortedByStart(services.currentEffectiveSegments)
    presentation = cfg.presentation
    notifications = cfg.notifications
    templateName = selectedTemplate(cfg)?.name ?? ""
    usingOverride = services.usingTodayOverride
  }

  var activeColor: Color {
    if let cur = snapshot.current {
      return WidgetPalette.kindColor(cur.kind)
    }
    return .accentColor
  }
}

struct MenuBarPopoverView: View {
  @ObservedObject var model: MenuBarPanelModel
  let onCommand: (MenuBarController.Command) -> Void

  var body: some View {
    let accent = model.activeColor
    let pres = model.presentation
    let muted = model.notifications.muted

    VStack(alignment: .leading, spacing: 12) {
      headerSection(accent: accent, muted: muted)

      if !model.effectiveSegments.isEmpty {
        compactTimelineStrip()
      }

      Divider()
        .opacity(0.45)

      quickToggleRow(visible: pres.visible, muted: muted)

      VStack(alignment: .leading, spacing: 9) {
        controlRow(title: "显示形式") {
          Picker(
            "显示形式",
            selection: Binding(
              get: { pres.view },
              set: { newView in
                switch newView {
                case .bar: onCommand(.viewBar)
                case .ring: onCommand(.viewRing)
                case .screenEdge: onCommand(.viewScreenEdge)
                }
              }
            )
          ) {
            Text("长条").tag(PresentationSettings.ViewMode.bar)
            Text("圆环").tag(PresentationSettings.ViewMode.ring)
            Text("边框").tag(PresentationSettings.ViewMode.screenEdge)
          }
          .pickerStyle(.segmented)
          .labelsHidden()
          .frame(maxWidth: .infinity)
        }

        controlRow(title: "显示范围") {
          Picker(
            "显示范围",
            selection: Binding(
              get: { pres.scope },
              set: { newScope in
                onCommand(newScope == .allDay ? .scopeAllDay : .scopeCurrentBlock)
              }
            )
          ) {
            Text("当前时段").tag(PresentationSettings.ScopeMode.currentBlock)
            Text("全天").tag(PresentationSettings.ScopeMode.allDay)
          }
          .pickerStyle(.segmented)
          .labelsHidden()
          .frame(maxWidth: .infinity)
        }

        controlRow(title: "顶部状态栏") {
          Picker(
            "顶部状态栏",
            selection: Binding(
              get: { pres.menuBarDisplay },
              set: { mode in
                onCommand(.menuBarDisplay(mode))
              }
            )
          ) {
            Text("仅图标").tag(PresentationSettings.MenuBarDisplayMode.iconOnly)
            Text("倒计时").tag(PresentationSettings.MenuBarDisplayMode.countdown)
            Text("节次").tag(PresentationSettings.MenuBarDisplayMode.detailed)
          }
          .pickerStyle(.segmented)
          .labelsHidden()
          .frame(maxWidth: .infinity)
        }
      }

      Divider()
        .opacity(0.45)

      footerActions()
    }
    .padding(16)
    .frame(width: 344, alignment: .leading)
    .fixedSize(horizontal: false, vertical: true)
  }

  // MARK: - 顶部实时状态卡片

  @ViewBuilder
  private func headerSection(accent: Color, muted: Bool) -> some View {
    let snap = model.snapshot
    let progress = (snap.current != nil) ? snap.progress : 0.0

    HStack(spacing: 13) {
      ZStack {
        Circle()
          .stroke(Color.primary.opacity(0.12), lineWidth: 4.5)

        if progress > 0.001 {
          Circle()
            .trim(from: 0, to: CGFloat(min(1.0, max(0.0, progress))))
            .stroke(accent, style: StrokeStyle(lineWidth: 4.5, lineCap: .round))
            .rotationEffect(.degrees(-90))
        }

        if snap.current != nil {
          Text(Formatters.percent(progress))
            .font(.system(size: 11.5, weight: .semibold, design: .rounded))
            .monospacedDigit()
            .foregroundStyle(.primary)
        } else {
          Image(systemName: snap.hasSchedule ? "checkmark" : "calendar")
            .font(.system(size: 14, weight: .semibold))
            .foregroundStyle(.secondary)
        }
      }
      .frame(width: 48, height: 48)

      VStack(alignment: .leading, spacing: 3) {
        HStack(spacing: 6) {
          Text(primaryTitle(snap: snap))
            .font(.system(size: 14.5, weight: .semibold))
            .foregroundStyle(.primary)
            .lineLimit(1)

          if model.usingOverride {
            Text("仅今天")
              .font(.system(size: 10.5, weight: .medium))
              .foregroundStyle(Color.accentColor)
              .padding(.horizontal, 6)
              .padding(.vertical, 1.5)
              .background(Capsule().fill(Color.accentColor.opacity(0.14)))
          }
        }

        Text(primarySubtitle(snap: snap))
          .font(.system(size: 12.5, weight: .medium, design: .rounded))
          .monospacedDigit()
          .foregroundStyle(snap.current != nil ? accent : .secondary)
          .lineLimit(1)

        HStack(spacing: 6) {
          Text(nextSegmentLine(snap: snap))
            .font(.system(size: 11.5))
            .foregroundStyle(.secondary)
            .lineLimit(1)

          if muted {
            Spacer(minLength: 4)
            Label("已暂停", systemImage: "bell.slash.fill")
              .font(.system(size: 10.5, weight: .medium))
              .foregroundStyle(.secondary)
          }
        }
      }
      Spacer(minLength: 0)
    }
  }

  // MARK: - 今日紧凑分节时间线

  @ViewBuilder
  private func compactTimelineStrip() -> some View {
    let segments = model.effectiveSegments
    let total = max(1, segments.reduce(0) { $0 + max(1, $1.durationMinutes) })
    let completedFocus = segments.filter {
      $0.kind == .focus && $0.end.totalMinutes <= model.clock.minutesSinceMidnight
    }.count
    let totalFocus = focusCount(segments)

    VStack(alignment: .leading, spacing: 6) {
      HStack {
        Text(model.templateName.isEmpty ? "今日作息" : model.templateName)
          .font(.system(size: 11.5, weight: .medium))
          .foregroundStyle(.secondary)
        Spacer()
        if totalFocus > 0 {
          Text("已完成 \(completedFocus) / \(totalFocus) 节")
            .font(.system(size: 11.5))
            .monospacedDigit()
            .foregroundStyle(.secondary)
        }
      }

      GeometryReader { geo in
        let gap: CGFloat = 2.5
        let avail = max(10, geo.size.width - gap * CGFloat(max(0, segments.count - 1)))
        HStack(spacing: gap) {
          ForEach(segments) { seg in
            let segWidth = max(4, avail * CGFloat(max(1, seg.durationMinutes)) / CGFloat(total))
            let segColor = WidgetPalette.kindColor(seg.kind)
            let segProgress = widgetSegmentProgress(segment: seg, now: model.clock, snapshot: model.snapshot)
            let height: CGFloat = (seg.kind == .shortBreak) ? 5.0 : 8.0

            ZStack(alignment: .leading) {
              Capsule()
                .fill(segColor.opacity(0.22))
                .frame(width: segWidth, height: height)
              if segProgress > 0.01 {
                Capsule()
                  .fill(segColor)
                  .frame(width: max(height * 0.5, segWidth * CGFloat(segProgress)), height: height)
              }
            }
            .help("\(seg.label)  \(Formatters.range(seg))（\(seg.durationMinutes) 分钟）")
          }
        }
        .frame(maxHeight: .infinity, alignment: .center)
      }
      .frame(height: 14)
      .frame(maxWidth: .infinity)
    }
    .padding(.horizontal, 10)
    .padding(.vertical, 8)
    .background(
      RoundedRectangle(cornerRadius: 10, style: .continuous)
        .fill(Color.primary.opacity(0.045))
        .overlay(
          RoundedRectangle(cornerRadius: 10, style: .continuous)
            .stroke(Color.primary.opacity(0.07), lineWidth: 0.6)
        )
    )
  }

  // MARK: - 快捷开关行（Liquid Glass 胶囊按钮组）

  @ViewBuilder
  private func quickToggleRow(visible: Bool, muted: Bool) -> some View {
    AdaptiveGlassGroup(spacing: 8) {
      HStack(spacing: 8) {
        Button {
          onCommand(.toggleOverlay)
        } label: {
          HStack(spacing: 6) {
            Image(systemName: visible ? "rectangle.inset.filled" : "rectangle.dashed")
              .font(.system(size: 12.5, weight: .semibold))
            Text(visible ? "桌面组件已开启" : "桌面组件已隐藏")
              .font(.system(size: 12, weight: .medium))
              .lineLimit(1)
          }
          .frame(maxWidth: .infinity)
          .padding(.vertical, 6)
        }
        .adaptiveGlassButton(prominent: visible)

        Button {
          onCommand(.toggleMute)
        } label: {
          HStack(spacing: 6) {
            Image(systemName: muted ? "bell.slash.fill" : "bell.fill")
              .font(.system(size: 12.5, weight: .semibold))
            Text(muted ? "提醒已暂停" : "提醒已开启")
              .font(.system(size: 12, weight: .medium))
              .lineLimit(1)
          }
          .frame(maxWidth: .infinity)
          .padding(.vertical, 6)
        }
        .adaptiveGlassButton(prominent: false)
      }
    }
  }

  @ViewBuilder
  private func controlRow<Control: View>(
    title: String,
    @ViewBuilder control: () -> Control
  ) -> some View {
    HStack(spacing: 10) {
      Text(title)
        .font(.system(size: 12.5, weight: .medium))
        .foregroundStyle(.secondary)
        .frame(width: 66, alignment: .leading)
      control()
    }
  }

  @ViewBuilder
  private func footerActions() -> some View {
    AdaptiveGlassGroup(spacing: 8) {
      HStack {
        Button {
          onCommand(.openSettings)
        } label: {
          Label("打开设置…", systemImage: "gearshape")
            .font(.system(size: 12.5, weight: .medium))
        }
        .adaptiveGlassButton(prominent: false)
        .keyboardShortcut(",", modifiers: [.command])

        Spacer()

        Button(role: .destructive) {
          onCommand(.quit)
        } label: {
          Label("退出应用", systemImage: "power")
            .font(.system(size: 12.5, weight: .medium))
        }
        .adaptiveGlassButton(prominent: false)
        .keyboardShortcut("q", modifiers: [.command])
      }
    }
  }

  // MARK: - 文案辅助

  private func primaryTitle(snap: Snapshot) -> String {
    if let cur = snap.current {
      if cur.kind == .focus {
        let ord = "第 \(cur.focusOrdinal) 节"
        return cur.label.hasPrefix(ord) ? cur.label : "\(ord) · \(cur.label)"
      }
      return cur.label
    }
    if snap.next != nil {
      let started = model.effectiveSegments.contains {
        $0.start.totalMinutes <= model.clock.minutesSinceMidnight
      }
      return started ? "当前为空档" : "今日尚未开始"
    }
    return snap.hasSchedule ? "今日作息已结束" : "今天暂无安排"
  }

  private func primarySubtitle(snap: Snapshot) -> String {
    if let cur = snap.current {
      let remaining = (cur.end.totalMinutes - model.clock.minutesSinceMidnight) * 60 - model.clock.second
      return "剩余 \(Formatters.remainingClock(remaining))  ·  \(Formatters.range(cur))"
    }
    if let nxt = snap.next {
      return "下一段 \(Formatters.clock(nxt.start)) · \(nxt.label)"
    }
    return "\(model.clock.dateKey) \(Formatters.weekdayLabel(model.clock.weekday))"
  }

  private func nextSegmentLine(snap: Snapshot) -> String {
    if let cur = snap.current {
      if let idx = model.effectiveSegments.firstIndex(where: { $0.id == cur.id }),
         idx + 1 < model.effectiveSegments.count {
        let nxt = model.effectiveSegments[idx + 1]
        return "下一段 \(Formatters.clock(nxt.start)) \(nxt.label)"
      }
      return "这是今天最后一段"
    }
    if let nxt = snap.next {
      return "范围 \(Formatters.range(nxt))"
    }
    return "打开设置可调整今日或常规模板"
  }
}
