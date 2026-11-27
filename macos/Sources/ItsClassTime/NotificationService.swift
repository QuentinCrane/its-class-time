// 系统即时通知适配器：仅在边界到达时使用 UNUserNotificationCenter (trigger: nil) 立即投递。
// 绝不预排整天计划通知，绝不绘制伪系统通知窗口。
import AppKit
import ClassTimeEngine
import Foundation
import UserNotifications

struct ComposedNotification: Equatable {
  var title: String
  var body: String
}

final class NotificationService: NSObject, UNUserNotificationCenterDelegate, @unchecked Sendable {
  static let shared = NotificationService()

  private var onActivate: (() -> Void)?
  private var authorizationSummary: String = "正在检查权限…"
  private var systemAuthorized: Bool = true
  private let lock = NSLock()

  var isRunningInAppBundle: Bool {
    Bundle.main.bundleURL.pathExtension == "app" && Bundle.main.bundleIdentifier != nil
  }

  func register(onActivate: @escaping () -> Void) {
    self.onActivate = onActivate
    guard isRunningInAppBundle else {
      setStatus("当前以解包命令行运行；打包为 ItsClassTime.app 后可调用 macOS 系统通知", authorized: false)
      return
    }
    let center = UNUserNotificationCenter.current()
    center.delegate = self
    refreshAuthorizationStatus(requestIfNeeded: true)
  }

  func refreshAuthorizationStatus(requestIfNeeded: Bool = false, completion: (() -> Void)? = nil) {
    guard isRunningInAppBundle else {
      setStatus("需通过 ItsClassTime.app 启动以使用系统通知", authorized: false)
      completion?()
      return
    }
    let center = UNUserNotificationCenter.current()
    center.getNotificationSettings { [weak self] settings in
      guard let self else { return }
      switch settings.authorizationStatus {
      case .authorized:
        self.setStatus("系统通知已允许", authorized: true)
        completion?()
      case .provisional:
        self.setStatus("系统通知已允许（静默投递）", authorized: true)
        completion?()
      case .ephemeral:
        self.setStatus("系统通知临时允许", authorized: true)
        completion?()
      case .denied:
        self.setStatus("系统已关闭本应用的通知（可在系统设置中开启）", authorized: false)
        completion?()
      case .notDetermined:
        if requestIfNeeded {
          center.requestAuthorization(options: [.alert, .sound]) { granted, _ in
            self.setStatus(
              granted ? "系统通知已允许" : "通知权限未授予（可在系统设置中开启）",
              authorized: granted
            )
            Services.shared.refresh()
            completion?()
          }
        } else {
          self.setStatus("尚未请求系统通知权限", authorized: false)
          completion?()
        }
      @unknown default:
        self.setStatus("系统通知状态未知", authorized: false)
        completion?()
      }
    }
  }

  var permissionLabel: String {
    lock.lock()
    defer { lock.unlock() }
    return authorizationSummary
  }

  var isSystemNotificationEnabled: Bool {
    lock.lock()
    defer { lock.unlock() }
    return systemAuthorized
  }

  private func setStatus(_ value: String, authorized: Bool) {
    lock.lock()
    authorizationSummary = value
    systemAuthorized = authorized
    lock.unlock()
  }

  func showBoundary(events: [BoundaryEvent]) {
    let settings = Services.shared.config.notifications
    guard let message = Self.compose(events: events, settings: settings) else { return }
    deliverImmediate(title: message.title, body: message.body, completion: nil)
  }

  func sendTestNotification(completion: @escaping (Bool, String) -> Void) {
    guard isRunningInAppBundle else {
      completion(false, "发送失败：请运行打包后的 ItsClassTime.app（macOS 要求应用包身份投递系统通知）。")
      return
    }
    let center = UNUserNotificationCenter.current()
    center.getNotificationSettings { [weak self] settings in
      guard let self else { return }
      if settings.authorizationStatus == .notDetermined {
        center.requestAuthorization(options: [.alert, .sound]) { granted, _ in
          self.refreshAuthorizationStatus()
          if granted {
            self.deliverImmediate(
              title: "通知正常",
              body: "到边界时，会像这样提醒你"
            ) { ok, detail in
              DispatchQueue.main.async { completion(ok, detail) }
            }
          } else {
            DispatchQueue.main.async {
              completion(false, "系统通知权限被拒绝，请在「系统设置 → 通知」中允许 It's Class Time!。")
            }
          }
        }
        return
      }
      if settings.authorizationStatus == .denied {
        self.setStatus("系统已关闭本应用的通知（可在系统设置中开启）", authorized: false)
        DispatchQueue.main.async {
          completion(false, "系统已关闭本应用的通知，请点击「打开系统通知设置」开启权限。")
        }
        return
      }
      self.deliverImmediate(
        title: "通知正常",
        body: "到边界时，会像这样提醒你"
      ) { ok, detail in
        DispatchQueue.main.async { completion(ok, detail) }
      }
    }
  }

  func openSystemNotificationSettings() {
    if let url = URL(string: "x-apple.systempreferences:com.apple.Notifications-Settings.extension") {
      NSWorkspace.shared.open(url)
    }
  }

  private func deliverImmediate(
    title: String,
    body: String,
    completion: ((Bool, String) -> Void)?
  ) {
    guard isRunningInAppBundle else {
      completion?(false, "非 .app 运行环境无法调用系统通知。")
      return
    }
    let content = UNMutableNotificationContent()
    content.title = title
    content.body = body
    content.sound = .default

    // trigger: nil -> 常驻进程即时投递，不创建未来计划队列。
    let request = UNNotificationRequest(
      identifier: "classtime.\(UUID().uuidString)",
      content: content,
      trigger: nil
    )
    UNUserNotificationCenter.current().add(request) { error in
      if let error {
        completion?(false, "投递失败：\(error.localizedDescription)")
      } else {
        completion?(true, "已请求 macOS 系统立即投递；专注模式或勿扰开启时由系统决定是否横幅弹出。")
      }
    }
  }

  // MARK: - UNUserNotificationCenterDelegate

  func userNotificationCenter(
    _ center: UNUserNotificationCenter,
    willPresent notification: UNNotification,
    withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void
  ) {
    completionHandler([.banner, .list, .sound])
  }

  func userNotificationCenter(
    _ center: UNUserNotificationCenter,
    didReceive response: UNNotificationResponse,
    withCompletionHandler completionHandler: @escaping () -> Void
  ) {
    let cb = onActivate
    DispatchQueue.main.async {
      cb?()
    }
    completionHandler()
  }

  // MARK: - 事件文案与相邻边界合并（与 Windows NotificationService 对等）

  static func compose(
    events rawEvents: [BoundaryEvent],
    settings: NotificationSettings
  ) -> ComposedNotification? {
    guard settings.enabled && !settings.muted else { return nil }

    func isAllowed(_ kind: BoundaryKind) -> Bool {
      switch kind {
      case .focusStart, .focusEnd:
        return settings.focusBoundaries
      case .lunchStart, .lunchEnd:
        return settings.lunchBoundaries
      case .breakStart, .breakEnd, .longBreakStart, .longBreakEnd:
        return settings.restBoundaries
      }
    }

    let events = rawEvents.filter { isAllowed($0.kind) }
    guard !events.isEmpty else { return nil }

    func priority(_ kind: BoundaryKind) -> Int {
      switch kind {
      case .focusStart, .focusEnd: return 5
      case .lunchStart, .lunchEnd: return 4
      case .longBreakStart, .longBreakEnd: return 3
      case .breakStart, .breakEnd: return 2
      }
    }

    func isStart(_ kind: BoundaryKind) -> Bool {
      switch kind {
      case .focusStart, .breakStart, .longBreakStart, .lunchStart: return true
      default: return false
      }
    }

    func ordinalText(_ seg: SegmentRef) -> String {
      "第 \(seg.focusOrdinal) 节"
    }

    func restText(_ seg: SegmentRef) -> String {
      switch seg.kind {
      case .lunch: return "午休时间，按今天的安排稍作休息"
      case .longBreak: return "长休息 \(Formatters.minutes(seg.durationMinutes))"
      case .shortBreak: return "课间休息 \(Formatters.minutes(seg.durationMinutes))"
      case .focus: return ""
      }
    }

    var focusEnd: BoundaryEvent?
    var bestStart: BoundaryEvent?
    var bestEnd: BoundaryEvent?

    for ev in events {
      if ev.kind == .focusEnd { focusEnd = ev }
      if isStart(ev.kind) {
        if bestStart == nil || priority(ev.kind) > priority(bestStart!.kind) {
          bestStart = ev
        }
      } else {
        if bestEnd == nil || priority(ev.kind) > priority(bestEnd!.kind) {
          bestEnd = ev
        }
      }
    }

    if let start = bestStart {
      let seg = start.segment
      switch start.kind {
      case .focusStart:
        return ComposedNotification(
          title: "开始啦",
          body: "\(ordinalText(seg))开始了（\(Formatters.range(seg))）"
        )
      case .lunchStart:
        let title = (focusEnd != nil) ? "下课了" : "午休时间"
        let body = if let fe = focusEnd {
          "\(ordinalText(fe.segment))结束，\(restText(seg))"
        } else {
          restText(seg)
        }
        return ComposedNotification(title: title, body: body)
      case .breakStart, .longBreakStart:
        let title = (focusEnd != nil) ? "下课了" : "休息一下"
        let body = if let fe = focusEnd {
          "\(ordinalText(fe.segment))结束，\(restText(seg))"
        } else {
          restText(seg)
        }
        return ComposedNotification(title: title, body: body)
      default:
        break
      }
    }

    if let fe = focusEnd {
      return ComposedNotification(
        title: "下课了",
        body: "\(ordinalText(fe.segment))结束，记得活动一下"
      )
    }

    if let end = bestEnd {
      switch end.kind {
      case .lunchEnd:
        return ComposedNotification(title: "午休结束", body: "接下来准备进入下午的安排")
      case .longBreakEnd:
        return ComposedNotification(title: "休息结束", body: "准备好下一段安排")
      case .breakEnd:
        return ComposedNotification(title: "继续加油", body: "课间结束，准备好下一节")
      default:
        return nil
      }
    }
    return nil
  }
}
