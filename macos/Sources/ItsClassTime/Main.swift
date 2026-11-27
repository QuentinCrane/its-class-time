// macOS 应用入口与常驻生命周期：
// 单实例互斥与唤醒、NSStatusItem 菜单栏常驻、NSPanel 悬浮窗、SwiftUI 设置窗、系统唤醒/时钟核查与即时通知。
import AppKit
import ClassTimeEngine
import Darwin
import Foundation

private let activateExistingNotificationName = Notification.Name("com.classtime.ItsClassTime.activate")

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
  private var overlay: OverlayController?
  private var settingsWindow: SettingsWindowController?
  private var menuBar: MenuBarController?
  private var monitor: BoundaryMonitor?
  private var observerToken: UUID?
  private var lockFileDescriptor: Int32 = -1
  private var isExiting = false

  func applicationDidFinishLaunching(_ notification: Notification) {
    let services = Services.shared
    let dataDir = Services.resolveDataDirectory()
    let isFirstLaunch = !FileManager.default.fileExists(
      atPath: dataDir.appendingPathComponent("config.json").path
    )

    try? FileManager.default.createDirectory(at: dataDir, withIntermediateDirectories: true)
    if !acquireSingleInstanceLock(dataDirectory: dataDir) {
      DistributedNotificationCenter.default().postNotificationName(
        activateExistingNotificationName,
        object: dataDir.path,
        userInfo: nil,
        deliverImmediately: true
      )
      NSApp.terminate(nil)
      return
    }

    guard services.initialize() else {
      let message =
        "无法安全读取、备份或写入配置，应用已停止启动。\n"
        + "原有配置或已创建的备份已保留。\n"
        + "请检查该文件及其文件夹的访问权限，然后重试。\n\n"
        + services.configPath.path
      if ProcessInfo.processInfo.environment["CLASSTIME_HEADLESS_SMOKE"] == nil {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .critical
        alert.messageText = "“上课”时间到！"
        alert.informativeText = message
        alert.addButton(withTitle: "好")
        alert.runModal()
      } else {
        fputs("\(message)\n", stderr)
      }
      releaseSingleInstanceLock()
      NSApp.terminate(nil)
      return
    }

    // 默认以菜单栏常驻模式启动，不抢占前台应用焦点；打开设置时再切换为标准窗口模式。
    NSApp.setActivationPolicy(.accessory)

    MainMenuBuilder.install(
      onOpenSettings: { [weak self] in self?.showSettings() },
      onQuit: { [weak self] in self?.exitApplication() }
    )

    NotificationService.shared.register { [weak self] in
      Task { @MainActor in
        self?.showSettings()
      }
    }

    let overlayController = OverlayController()
    let menuBarController = MenuBarController { [weak self] cmd in
      self?.handleMenuCommand(cmd)
    }
    overlayController.onDisplayTick = { [weak menuBarController] in
      menuBarController?.refresh()
    }
    overlay = overlayController
    menuBar = menuBarController

    observerToken = services.addObserver { [weak self] change in
      guard let self, !self.isExiting else { return }
      if change == .all {
        self.monitor?.requestRefresh()
      }
      self.applyState(presentationOnly: change == .presentation)
    }

    let boundaryMonitor = BoundaryMonitor()
    boundaryMonitor.start { [weak self] batch in
      Task { @MainActor in
        guard let self, !self.isExiting else { return }
        NotificationService.shared.showBoundary(events: batch.events)
        Services.shared.refresh()
        self.applyState(presentationOnly: false)
      }
    }
    monitor = boundaryMonitor

    registerSystemObservers()
    applyState(presentationOnly: false)

    let env = ProcessInfo.processInfo.environment
    if env["CLASSTIME_SHOW_SETTINGS"] != nil || isFirstLaunch {
      if env["CLASSTIME_HEADLESS_SMOKE"] == nil {
        showSettings()
      }
    }

    // 可选自动化冒烟检查：验证单进程启动、状态加载与悬浮窗装配后安全退出
    if let smokeSecStr = env["CLASSTIME_SMOKE_EXIT_AFTER_MS"],
       let ms = Double(smokeSecStr), ms > 0 {
      DispatchQueue.main.asyncAfter(deadline: .now() + ms / 1000.0) { [weak self] in
        self?.exitApplication()
      }
    }
  }

  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    // 关闭设置窗口 != 退出应用
    false
  }

  func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
    showSettings()
    return true
  }

  func applicationWillTerminate(_ notification: Notification) {
    cleanupBeforeExit()
  }

  // MARK: - 单实例锁与二次打开激活

  private func releaseSingleInstanceLock() {
    if lockFileDescriptor >= 0 {
      _ = lockf(lockFileDescriptor, F_ULOCK, 0)
      Darwin.close(lockFileDescriptor)
      lockFileDescriptor = -1
    }
  }

  private func acquireSingleInstanceLock(dataDirectory: URL) -> Bool {
    let lockPath = dataDirectory.appendingPathComponent("instance.lock").path
    let fd = Darwin.open(lockPath, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)
    guard fd >= 0 else { return true }
    if lockf(fd, F_TLOCK, 0) != 0 {
      Darwin.close(fd)
      return false
    }
    lockFileDescriptor = fd

    let targetPath = dataDirectory.path
    DistributedNotificationCenter.default().addObserver(
      forName: activateExistingNotificationName,
      object: targetPath,
      queue: .main
    ) { [weak self] _ in
      Task { @MainActor in
        self?.showSettings()
      }
    }
    return true
  }

  // MARK: - 睡眠唤醒、系统时钟与时区变化监听（重锚当下，不补发旧事件）

  private func registerSystemObservers() {
    let wsCenter = NSWorkspace.shared.notificationCenter
    wsCenter.addObserver(
      forName: NSWorkspace.didWakeNotification,
      object: nil,
      queue: .main
    ) { [weak self] _ in
      Task { @MainActor in
        self?.handleClockOrWakeChange()
      }
    }

    let nc = NotificationCenter.default
    for name in [
      Notification.Name.NSSystemClockDidChange,
      Notification.Name.NSSystemTimeZoneDidChange,
      Notification.Name.NSCalendarDayChanged,
    ] {
      nc.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
        Task { @MainActor in
          self?.handleClockOrWakeChange()
        }
      }
    }
  }

  private func handleClockOrWakeChange() {
    guard !isExiting else { return }
    monitor?.requestRefresh()
    Services.shared.refresh()
    applyState(presentationOnly: false)
  }

  // MARK: - 界面同步与菜单命令

  private func applyState(presentationOnly: Bool) {
    guard !isExiting else { return }
    let cfg = Services.shared.config

    if let overlay {
      if cfg.presentation.visible {
        if !overlay.isVisible {
          overlay.showOverlay()
        } else {
          overlay.applyPresentation(animate: presentationOnly)
        }
      } else if overlay.isVisible {
        overlay.hideOverlay()
      }
      menuBar?.setOverlayTicking(overlay.isVisible && cfg.presentation.visible)
    }
    menuBar?.refresh()
  }

  private func showSettings() {
    if settingsWindow == nil {
      settingsWindow = SettingsWindowController { [weak self] in
        self?.exitApplication()
      }
    }
    settingsWindow?.show()
  }

  private func handleMenuCommand(_ command: MenuBarController.Command) {
    let services = Services.shared
    var pres = services.config.presentation

    switch command {
    case .openSettings:
      showSettings()
    case .toggleOverlay:
      pres.visible.toggle()
      services.updatePresentation(pres)
    case .viewBar:
      pres.view = .bar
      services.updatePresentation(pres)
    case .viewRing:
      pres.view = .ring
      services.updatePresentation(pres)
    case .viewScreenEdge:
      pres.view = .screenEdge
      services.updatePresentation(pres)
    case .scopeCurrentBlock:
      pres.scope = .currentBlock
      services.updatePresentation(pres)
    case .scopeAllDay:
      pres.scope = .allDay
      services.updatePresentation(pres)
    case .menuBarDisplay(let mode):
      pres.menuBarDisplay = mode
      services.updatePresentation(pres)
    case .glassStyle(let style):
      pres.glassStyle = style
      services.updatePresentation(pres)
    case .toggleMute:
      services.setMuted(!services.config.notifications.muted)
    case .quit:
      exitApplication()
    }
  }

  func exitApplication() {
    guard !isExiting else { return }
    cleanupBeforeExit()
    NSApp.terminate(nil)
  }

  private func cleanupBeforeExit() {
    guard !isExiting else { return }
    isExiting = true
    if let token = observerToken {
      Services.shared.removeObserver(token)
      observerToken = nil
    }
    monitor?.stop()
    monitor = nil
    menuBar?.destroy()
    menuBar = nil
    overlay?.close()
    overlay = nil
    settingsWindow?.forceCloseForExit()
    settingsWindow = nil
    Services.shared.save()
    releaseSingleInstanceLock()
  }
}

@main
struct ItsClassTimeMain {
  static func main() {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.run()
  }
}
