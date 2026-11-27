// 作息时间轴交互编辑器与周/日课表预览网格：
// - 时间轴支持 40–200 pt/h 缩放、1/5/15 分钟拖动步进（仅对拖动量吸附，保留原分钟精度）、
//   整体拖动/左右调整边界、实时重叠红框预警、松手冲突自动回弹、双击时段编辑与双击空白新增。
// - 课表预览支持常规模板 7 天周视图与今日单日视图，点击任意块直接打开编辑器。
import AppKit
import ClassTimeEngine
import SwiftUI

@MainActor
final class TimelineEditorViewModel: ObservableObject {
  @Published var pxPerHour: Double = 90.0
  @Published var draggingSegmentId: String? = nil
  @Published var dragMode: TimelineDragMode = .none
  @Published var dragOriginStart: Int = 0
  @Published var dragOriginEnd: Int = 0
  @Published var dragCurrentStart: Int = 0
  @Published var dragCurrentEnd: Int = 0
  @Published var hasInitialScroll: Bool = false

  func beginDrag(segment: Segment, mode: TimelineDragMode) {
    draggingSegmentId = segment.id
    dragMode = mode
    dragOriginStart = segment.start.totalMinutes
    dragOriginEnd = segment.end.totalMinutes
    dragCurrentStart = segment.start.totalMinutes
    dragCurrentEnd = segment.end.totalMinutes
  }

  func updateDrag(deltaX: CGFloat, stepMinutes: Int) {
    guard dragMode != .none else { return }
    let deltaMinutes = Int((Double(deltaX) * 60.0 / max(20.0, pxPerHour)).rounded())
    let updated = draggedTimelineRange(
      original: TimelineRange(start: dragOriginStart, end: dragOriginEnd),
      mode: dragMode,
      deltaMinutes: deltaMinutes,
      stepMinutes: stepMinutes
    )
    dragCurrentStart = updated.start
    dragCurrentEnd = updated.end
  }

  func endDrag() -> (id: String, start: Int, end: Int, changed: Bool)? {
    guard let id = draggingSegmentId, dragMode != .none else {
      resetDrag()
      return nil
    }
    let start = dragCurrentStart
    let end = dragCurrentEnd
    let changed = (start != dragOriginStart || end != dragOriginEnd)
    resetDrag()
    return (id, start, end, changed)
  }

  func resetDrag() {
    draggingSegmentId = nil
    dragMode = .none
    dragOriginStart = 0
    dragOriginEnd = 0
    dragCurrentStart = 0
    dragCurrentEnd = 0
  }
}

struct InteractiveTimelineView: View {
  @ObservedObject var model: TimelineEditorViewModel
  let segments: [Segment]
  let viewingTodayPlan: Bool
  let snapMinutes: Int
  let onChangeSnap: (Int) -> Void
  let onCommitDrag: (String, Int, Int) -> Void
  let onEditSegment: (Segment) -> Void
  let onCreateAtMinute: (Int) -> Void
  let onNoticeError: (String) -> Void

  private let canvasHeight: CGFloat = 104
  private let headerHeight: CGFloat = 28

  private func xForMinute(_ minute: Int) -> CGFloat {
    CGFloat(Double(minute) * model.pxPerHour / 60.0)
  }

  private func minuteForX(_ x: CGFloat) -> Int {
    Int((Double(x) * 60.0 / max(20.0, model.pxPerHour)).rounded())
  }

  private func isOverlapping(dragId: String, start: Int, end: Int) -> Bool {
    segments.contains { other in
      other.id != dragId && start < other.end.totalMinutes && other.start.totalMinutes < end
    }
  }

  var body: some View {
    let pxPerHour = max(40.0, model.pxPerHour)
    let totalWidth = CGFloat(24.0 * pxPerHour)
    let labelStepHours = pxPerHour >= 55.0 ? 1 : 2
    let now = nowClock()
    let nowMinute = now.minutesSinceMidnight

    VStack(alignment: .leading, spacing: 10) {
      Text("拖动时段或两端调整时间；双击时段编辑，双击空白处新增。")
        .font(.system(size: 12))
        .foregroundStyle(.secondary)

      HStack(spacing: 16) {
        HStack(spacing: 8) {
          Text("缩放")
            .font(.system(size: 13))
          Slider(value: $model.pxPerHour, in: 40...200, step: 10)
            .frame(width: 150)
            .accessibilityLabel("时间轴缩放")
        }

        HStack(spacing: 8) {
          Text("拖动步进")
            .font(.system(size: 13))
          Picker(
            "拖动步进",
            selection: Binding(
              get: { snapMinutes },
              set: { onChangeSnap($0) }
            )
          ) {
            Text("1 分钟").tag(1)
            Text("5 分钟").tag(5)
            Text("15 分钟").tag(15)
          }
          .labelsHidden()
          .frame(width: 96)
          .accessibilityLabel("时间轴拖动步进")
        }
        Spacer()
      }

      ScrollViewReader { proxy in
        ScrollView(.horizontal, showsIndicators: true) {
          ZStack(alignment: .topLeading) {
            // 不可见的整点锚点，用于首次打开时定位到首段附近。
            HStack(spacing: 0) {
              ForEach(0..<24, id: \.self) { hour in
                Color.clear
                  .frame(width: CGFloat(pxPerHour), height: 1)
                  .id("timeline-hour-\(hour)")
              }
            }

            // 空白背景命中区：双击空白处新增时段
            Color.clear
              .frame(width: totalWidth, height: canvasHeight)
              .contentShape(Rectangle())
              .gesture(
                SpatialTapGesture(count: 2).onEnded { value in
                  guard model.dragMode == .none else { return }
                  let rawMin = minuteForX(value.location.x)
                  let snapped = max(0, snapToTimelineStep(rawMin, step: snapMinutes))
                  if snapped >= 1439 {
                    onNoticeError("所选时间太晚，请选择 23:58 或更早的时间新增时段。")
                    return
                  }
                  onCreateAtMinute(snapped)
                }
              )

            // 刻度线与整点标签
            Canvas { ctx, size in
              let stroke = Color.primary.opacity(0.14)
              for hour in 0...24 {
                let x = CGFloat(Double(hour) * pxPerHour)
                let tickHeight: CGFloat = (hour % labelStepHours == 0) ? 10 : 5
                ctx.fill(
                  Path(CGRect(x: x - 0.5, y: headerHeight - tickHeight, width: 1, height: tickHeight)),
                  with: .color(stroke)
                )
              }
              ctx.fill(
                Path(CGRect(x: 0, y: headerHeight, width: size.width, height: 1)),
                with: .color(stroke)
              )
              if nowMinute >= 0 && nowMinute < 24 * 60 {
                let nx = xForMinute(nowMinute)
                ctx.fill(
                  Path(CGRect(x: nx - 1, y: headerHeight, width: 2, height: size.height - headerHeight)),
                  with: .color(Color.accentColor.opacity(0.55))
                )
              }
            }
            .frame(width: totalWidth, height: canvasHeight)
            .allowsHitTesting(false)

            ForEach(0..<24, id: \.self) { hour in
              if hour % labelStepHours == 0 {
                Text(String(format: "%02d:00", hour))
                  .font(.system(size: 11))
                  .monospacedDigit()
                  .foregroundStyle(.secondary)
                  .offset(x: CGFloat(Double(hour) * pxPerHour) + 4, y: 2)
                  .allowsHitTesting(false)
              }
            }

            // 时段块
            ForEach(segments) { segment in
              let isDraggingThis = (model.draggingSegmentId == segment.id)
              let displayStart = isDraggingThis ? model.dragCurrentStart : segment.start.totalMinutes
              let displayEnd = isDraggingThis ? model.dragCurrentEnd : segment.end.totalMinutes
              let startX = xForMinute(displayStart)
              let blockWidth = max(16.0, xForMinute(displayEnd) - startX)
              let overlap = isDraggingThis && isOverlapping(dragId: segment.id, start: displayStart, end: displayEnd)

              TimelineSegmentBlockView(
                segment: segment,
                displayStart: displayStart,
                displayEnd: displayEnd,
                width: blockWidth,
                isOverlap: overlap,
                onDragBody: { deltaX in
                  if model.draggingSegmentId == nil {
                    model.beginDrag(segment: segment, mode: .move)
                  }
                  model.updateDrag(deltaX: deltaX, stepMinutes: snapMinutes)
                },
                onDragLeft: { deltaX in
                  if model.draggingSegmentId == nil {
                    model.beginDrag(segment: segment, mode: .resizeStart)
                  }
                  model.updateDrag(deltaX: deltaX, stepMinutes: snapMinutes)
                },
                onDragRight: { deltaX in
                  if model.draggingSegmentId == nil {
                    model.beginDrag(segment: segment, mode: .resizeEnd)
                  }
                  model.updateDrag(deltaX: deltaX, stepMinutes: snapMinutes)
                },
                onDragEnd: {
                  if let result = model.endDrag(), result.changed {
                    onCommitDrag(result.id, result.start, result.end)
                  }
                },
                onDoubleClick: {
                  onEditSegment(segment)
                }
              )
              .offset(x: startX, y: headerHeight + 6)
              .zIndex(isDraggingThis ? 10 : 1)
            }
          }
          .frame(width: totalWidth, height: canvasHeight, alignment: .topLeading)
          .coordinateSpace(name: "timelineCanvas")
        }
        .frame(height: canvasHeight + 12)
        .background(
          RoundedRectangle(cornerRadius: 8, style: .continuous)
            .fill(Color.primary.opacity(0.03))
            .overlay(
              RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(Color.primary.opacity(0.10), lineWidth: 1)
            )
        )
        .accessibilityLabel("作息时间轴")
        .onAppear {
          if !model.hasInitialScroll, let first = segments.first {
            model.hasInitialScroll = true
            let targetHour = max(0, first.start.hour - 1)
            DispatchQueue.main.async {
              proxy.scrollTo("timeline-hour-\(targetHour)", anchor: .leading)
            }
          }
        }
      }

      Text(timelineStatusLine())
        .font(.system(size: 12))
        .foregroundStyle(isCurrentlyOverlapping ? Color.red : Color.secondary)
    }
  }

  private var isCurrentlyOverlapping: Bool {
    guard let id = model.draggingSegmentId, model.dragMode != .none else { return false }
    return isOverlapping(dragId: id, start: model.dragCurrentStart, end: model.dragCurrentEnd)
  }

  private func timelineStatusLine() -> String {
    if let id = model.draggingSegmentId, model.dragMode != .none,
       let startTod = TimeOfDay.fromTotalMinutes(model.dragCurrentStart),
       let endTod = TimeOfDay.fromTotalMinutes(model.dragCurrentEnd) {
      let rangeText = "\(startTod.descriptionText)–\(endTod.descriptionText)"
      if isOverlapping(dragId: id, start: model.dragCurrentStart, end: model.dragCurrentEnd) {
        return "⚠ 与其他时段重叠，松手将恢复原时间。\(rangeText)"
      }
      return "正在调整（步进 \(snapMinutes) 分钟）：\(rangeText)"
    }
    if segments.isEmpty {
      return "暂无时段：双击时间轴空白处，在对应时刻新增。"
    }
    return viewingTodayPlan
      ? "显示今天的计划；拖动调整只影响今天。"
      : "显示常规模板时段；拖动调整会保存到模板。"
  }
}

private struct TimelineSegmentBlockView: View {
  let segment: Segment
  let displayStart: Int
  let displayEnd: Int
  let width: CGFloat
  let isOverlap: Bool
  let onDragBody: (CGFloat) -> Void
  let onDragLeft: (CGFloat) -> Void
  let onDragRight: (CGFloat) -> Void
  let onDragEnd: () -> Void
  let onDoubleClick: () -> Void

  var body: some View {
    let kindColor = WidgetPalette.kindColor(segment.kind)
    let borderColor = isOverlap ? Color.red : kindColor
    let rangeText: String = {
      let s = TimeOfDay.fromTotalMinutes(displayStart)?.descriptionText ?? segment.start.descriptionText
      let e = TimeOfDay.fromTotalMinutes(displayEnd)?.descriptionText ?? segment.end.descriptionText
      return "\(s)–\(e)"
    }()

    HStack(spacing: 0) {
      // 左侧调整把手
      gripBar(color: borderColor)
        .frame(width: min(12, width * 0.25), height: 54)
        .contentShape(Rectangle())
        .highPriorityGesture(
          DragGesture(minimumDistance: 2, coordinateSpace: .named("timelineCanvas"))
            .onChanged { value in onDragLeft(value.translation.width) }
            .onEnded { _ in onDragEnd() }
        )

      // 中间内容与整体移动区
      VStack(alignment: .leading, spacing: 2) {
        if width >= 44 {
          Text(segment.label)
            .font(.system(size: 11.5, weight: .semibold))
            .foregroundStyle(.primary)
            .lineLimit(1)
          Text(rangeText)
            .font(.system(size: 10.5))
            .monospacedDigit()
            .foregroundStyle(.secondary)
            .lineLimit(1)
        }
      }
      .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
      .padding(.horizontal, 2)
      .contentShape(Rectangle())
      .gesture(
        DragGesture(minimumDistance: 3, coordinateSpace: .named("timelineCanvas"))
          .onChanged { value in onDragBody(value.translation.width) }
          .onEnded { _ in onDragEnd() }
      )
      .onTapGesture(count: 2) {
        onDoubleClick()
      }

      // 右侧调整把手
      gripBar(color: borderColor)
        .frame(width: min(12, width * 0.25), height: 54)
        .contentShape(Rectangle())
        .highPriorityGesture(
          DragGesture(minimumDistance: 2, coordinateSpace: .named("timelineCanvas"))
            .onChanged { value in onDragRight(value.translation.width) }
            .onEnded { _ in onDragEnd() }
        )
    }
    .frame(width: width, height: 54)
    .background(
      RoundedRectangle(cornerRadius: 7, style: .continuous)
        .fill(kindColor.opacity(0.22))
        .overlay(
          RoundedRectangle(cornerRadius: 7, style: .continuous)
            .stroke(borderColor, lineWidth: isOverlap ? 1.8 : 1.1)
        )
    )
    .help("\(segment.label)  \(rangeText)")
    .accessibilityElement(children: .ignore)
    .accessibilityLabel("时间轴时段 \(segment.label) \(rangeText)")
    .accessibilityHint("拖动移动时段，双击打开编辑")
  }

  @ViewBuilder
  private func gripBar(color: Color) -> some View {
    if width >= 28 {
      RoundedRectangle(cornerRadius: 2, style: .continuous)
        .fill(color.opacity(0.68))
        .frame(width: 3.5, height: 22)
    } else {
      Color.clear
    }
  }
}

// MARK: - 周/日课表网格预览

struct WeeklyTimetableGridView: View {
  let segments: [Segment]
  let schedule: Schedule?
  let permanent: Bool
  let todayDateKey: String
  let onEditSegment: (Segment) -> Void

  private let headerHeight: CGFloat = 30
  private let hourHeight: CGFloat = 53
  private let timeColumnWidth: CGFloat = 52

  var body: some View {
    if segments.isEmpty {
      Text(permanent ? "模板还没有安排时段。" : "今天没有安排的时段。")
        .font(.system(size: 12.5))
        .foregroundStyle(.secondary)
        .padding(.vertical, 8)
    } else {
      let firstHour = segments.first!.start.hour
      let endMinutes = segments.map(\.end.totalMinutes).max() ?? ((firstHour + 1) * 60)
      let lastHour = max(firstHour + 1, (endMinutes + 59) / 60)
      let bodyHeight = CGFloat(lastHour - firstHour) * hourHeight
      let days = permanent ? 7 : 1

      ScrollView([.horizontal, .vertical], showsIndicators: true) {
        HStack(alignment: .top, spacing: 0) {
          // 左侧小时列
          ZStack(alignment: .topLeading) {
            Color.clear.frame(width: timeColumnWidth, height: headerHeight + bodyHeight)
            ForEach(firstHour..<lastHour, id: \.self) { hour in
              let y = headerHeight + CGFloat(hour - firstHour) * hourHeight
              Text(String(format: "%02d:00", hour))
                .font(.system(size: 11.5))
                .monospacedDigit()
                .foregroundStyle(.secondary)
                .offset(x: 4, y: y + 2)
            }
          }

          // 各天列
          ForEach(0..<days, id: \.self) { dayIndex in
            let weekday = dayIndex + 1
            let active = !permanent || (schedule?.isActive(on: weekday) ?? false)
            let heading = permanent
              ? (active ? Formatters.weekdayLabel(weekday) : "\(Formatters.weekdayLabel(weekday)) · 休息")
              : "今天  \(todayDateKey)"

            ZStack(alignment: .topLeading) {
              // 顶部列标题 + 小时横线
              VStack(spacing: 0) {
                Text(heading)
                  .font(.system(size: 12.5, weight: .semibold))
                  .foregroundStyle(active ? Color.primary : Color.secondary)
                  .frame(maxWidth: .infinity, alignment: permanent ? .center : .leading)
                  .padding(.horizontal, 6)
                  .frame(height: headerHeight)

                ZStack(alignment: .topLeading) {
                  ForEach(firstHour..<lastHour, id: \.self) { hour in
                    let y = CGFloat(hour - firstHour) * hourHeight
                    Rectangle()
                      .fill(Color.primary.opacity(0.10))
                      .frame(height: 1)
                      .offset(y: y)
                  }

                  if active {
                    ForEach(segments) { seg in
                      let startOffset = CGFloat(seg.start.totalMinutes - firstHour * 60) * hourHeight / 60.0
                      let blockHeight = max(4.0, CGFloat(seg.durationMinutes) * hourHeight / 60.0 - 1.5)
                      let kindColor = WidgetPalette.kindColor(seg.kind)
                      let caption = permanent
                        ? seg.label
                        : "\(seg.label)  ·  \(Formatters.range(seg))"

                      Button {
                        onEditSegment(seg)
                      } label: {
                        HStack {
                          if blockHeight >= 18 {
                            Text(caption)
                              .font(.system(size: 11.5, weight: .medium))
                              .foregroundStyle(.primary)
                              .lineLimit(1)
                              .truncationMode(.tail)
                          }
                          Spacer(minLength: 0)
                        }
                        .padding(.horizontal, 5)
                        .frame(maxWidth: .infinity, minHeight: blockHeight, maxHeight: blockHeight)
                        .background(
                          RoundedRectangle(cornerRadius: 5, style: .continuous)
                            .fill(kindColor.opacity(0.24))
                            .overlay(
                              RoundedRectangle(cornerRadius: 5, style: .continuous)
                                .stroke(kindColor.opacity(0.45), lineWidth: 0.7)
                            )
                        )
                      }
                      .buttonStyle(.plain)
                      .padding(.horizontal, 3)
                      .offset(y: startOffset)
                      .help("\(seg.label)  \(Formatters.range(seg))")
                      .accessibilityLabel("课表编辑 \(heading) \(seg.label) \(Formatters.range(seg))")
                    }
                  }
                }
                .frame(height: bodyHeight, alignment: .topLeading)
              }
              .overlay(alignment: .leading) {
                Rectangle()
                  .fill(Color.primary.opacity(0.10))
                  .frame(width: 1)
                  .padding(.top, headerHeight)
              }
            }
            .frame(minWidth: permanent ? 88 : 300, maxWidth: .infinity)
          }
        }
        .frame(minWidth: permanent ? 668 : 352)
      }
      .frame(maxHeight: 420)
      .accessibilityLabel("课表预览")
    }
  }
}
