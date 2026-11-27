// 边界等待线程：按下一边界精确休眠 + 60 秒看门狗兜底，绝不采用 1 秒忙轮询。
// 启动、唤醒、时区/时钟变化及保存配置后重锚当下时刻，30 秒外过期事件一律跳过。
import ClassTimeEngine
import Foundation

struct FiredBatch: Sendable {
  var events: [BoundaryEvent]
  var plan: [Segment]
  var revision: Int
  var dateKey: String
}

final class BoundaryMonitor: @unchecked Sendable {
  typealias FireCallback = @Sendable (FiredBatch) -> Void

  private let condition = NSCondition()
  private var thread: Thread?
  private var stopping = false
  private var refreshRequested = false
  private var onFire: FireCallback?

  func start(_ callback: @escaping FireCallback) {
    stop()
    condition.lock()
    stopping = false
    refreshRequested = false
    onFire = callback
    condition.unlock()

    let worker = Thread { [weak self] in
      self?.runLoop()
    }
    worker.name = "ClassTime.BoundaryMonitor"
    worker.qualityOfService = .utility
    thread = worker
    worker.start()
  }

  func stop() {
    condition.lock()
    stopping = true
    onFire = nil
    condition.broadcast()
    condition.unlock()
    thread = nil
  }

  func requestRefresh() {
    condition.lock()
    refreshRequested = true
    condition.broadcast()
    condition.unlock()
  }

  private struct Candidate {
    var seconds: Int
    var event: BoundaryEvent
  }

  private func collectBoundaries(_ segments: [Segment]) -> [Candidate] {
    let sorted = sortedByStart(segments)
    var candidates: [Candidate] = []
    candidates.reserveCapacity(sorted.count * 2)

    var focusOrdinal = 0
    for segment in sorted {
      if segment.kind == .focus { focusOrdinal += 1 }
      let ref = SegmentRef(
        id: segment.id,
        label: segment.label,
        kind: segment.kind,
        start: segment.start,
        end: segment.end,
        focusOrdinal: segment.kind == .focus ? focusOrdinal : 0
      )
      candidates.append(
        Candidate(
          seconds: segment.start.totalMinutes * 60,
          event: BoundaryEvent(kind: startBoundaryKind(segment.kind), segment: ref, at: WallClock())
        )
      )
      candidates.append(
        Candidate(
          seconds: segment.end.totalMinutes * 60,
          event: BoundaryEvent(kind: endBoundaryKind(segment.kind), segment: ref, at: WallClock())
        )
      )
    }

    return candidates.enumerated()
      .sorted { lhs, rhs in
        if lhs.element.seconds != rhs.element.seconds {
          return lhs.element.seconds < rhs.element.seconds
        }
        return lhs.offset < rhs.offset
      }
      .map(\.element)
  }

  private func runLoop() {
    let anchor = nowClock()
    var lastCheckedSeconds = anchor.secondsSinceMidnight
    var activeDate = anchor.dateKey
    Services.shared.forgetNotificationsForOtherDays(activeDate)

    let lateToleranceSeconds = 30
    let watchdogSeconds = 60

    while true {
      condition.lock()
      if stopping {
        condition.unlock()
        return
      }
      if refreshRequested {
        refreshRequested = false
        let current = nowClock()
        lastCheckedSeconds = current.secondsSinceMidnight
        activeDate = current.dateKey
      }
      condition.unlock()

      let now = nowClock()
      let plan = Services.shared.plan(for: now)
      let candidates = collectBoundaries(plan.segments)

      if now.dateKey != activeDate {
        activeDate = now.dateKey
        Services.shared.forgetNotificationsForOtherDays(activeDate)
        lastCheckedSeconds = 0
        Services.shared.refresh()
      }

      let nowSeconds = now.secondsSinceMidnight
      var due: [Candidate] = []
      for candidate in candidates {
        if candidate.seconds <= lastCheckedSeconds || candidate.seconds > nowSeconds {
          continue
        }
        if nowSeconds - candidate.seconds > lateToleranceSeconds {
          continue
        }
        var ev = candidate.event
        ev.at = WallClock(
          year: now.year,
          month: now.month,
          day: now.day,
          hour: candidate.seconds / 3600,
          minute: (candidate.seconds % 3600) / 60,
          second: candidate.seconds % 60
        )
        due.append(Candidate(seconds: candidate.seconds, event: ev))
      }

      if !due.isEmpty {
        var freshEvents: [BoundaryEvent] = []
        for candidate in due {
          let key = eventKey(
            dateKey: plan.dateKey,
            segmentId: candidate.event.segment.id,
            kind: candidate.event.kind,
            planRevision: plan.revision
          )
          if Services.shared.wasNotified(key) { continue }
          Services.shared.markNotified(key)
          freshEvents.append(candidate.event)
        }
        if !freshEvents.isEmpty {
          let batch = FiredBatch(
            events: freshEvents,
            plan: plan.segments,
            revision: plan.revision,
            dateKey: plan.dateKey
          )
          condition.lock()
          let cb = onFire
          condition.unlock()
          cb?(batch)
        }
      }

      lastCheckedSeconds = nowSeconds

      var waitSeconds = watchdogSeconds
      for candidate in candidates where candidate.seconds > nowSeconds {
        let delta = candidate.seconds - nowSeconds
        if delta < waitSeconds {
          waitSeconds = max(1, delta)
        }
        break
      }

      let deadline = Date().addingTimeInterval(TimeInterval(waitSeconds))
      condition.lock()
      while !stopping && !refreshRequested {
        if !condition.wait(until: deadline) {
          break
        }
      }
      if stopping {
        condition.unlock()
        return
      }
      condition.unlock()
    }
  }
}
