// 原生设置窗口控制器：关闭窗口只隐藏不退出；打开时切换为标准窗口激活策略，关闭后退回菜单栏常驻。
import AppKit
import SwiftUI

@MainActor
final class SettingsWindowController: NSObject, NSWindowDelegate {
  private var window: NSWindow?
  private let settingsViewModel = SettingsViewModel()
  private var allowTerminateClose = false
  private let onRequestQuit: () -> Void

  init(onRequestQuit: @escaping () -> Void) {
    self.onRequestQuit = onRequestQuit
    super.init()
    NotificationCenter.default.addObserver(
      forName: NSApplication.didChangeScreenParametersNotification,
      object: nil,
      queue: .main
    ) { [weak self] _ in
      Task { @MainActor in
        self?.settingsViewModel.refreshDisplays()
      }
    }
  }

  func show() {
    if window == nil {
      buildWindow()
    }
    guard let window else { return }

    settingsViewModel.refreshDisplays()
    settingsViewModel.syncDraftsFromConfig()
    settingsViewModel.setWindowVisible(true)
    Services.shared.refreshLaunchAtLoginStatus()

    NotificationService.shared.refreshAuthorizationStatus { [weak self] in
      Task { @MainActor in
        self?.settingsViewModel.syncPermissionState()
        Services.shared.tickSnapshotOnly()
      }
    }

    NSApp.setActivationPolicy(.regular)
    if window.isMiniaturized {
      window.deminiaturize(nil)
    }
    NSApp.activate(ignoringOtherApps: true)
    window.makeKeyAndOrderFront(nil)
    window.orderFrontRegardless()

    // 从菜单栏常驻(.accessory)切到常规窗口(.regular)时，下一轮主循环再次置前以确保可靠唤出。
    DispatchQueue.main.async { [weak window] in
      guard let window else { return }
      NSApp.activate(ignoringOtherApps: true)
      window.makeKeyAndOrderFront(nil)
      window.orderFrontRegardless()
    }
  }

  func forceCloseForExit() {
    settingsViewModel.flushPendingCommits()
    settingsViewModel.setWindowVisible(false)
    allowTerminateClose = true
    window?.close()
    window = nil
  }

  private func buildWindow() {
    let rootView = SettingsRootView(viewModel: settingsViewModel, onQuit: onRequestQuit)
    let hosting = NSHostingController(rootView: rootView)

    let win = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 1140, height: 780),
      styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
      backing: .buffered,
      defer: false
    )
    win.title = "“上课”时间到！"
    win.isOpaque = false
    win.backgroundColor = .clear
    win.titlebarAppearsTransparent = true
    win.toolbarStyle = .unified
    win.titlebarSeparatorStyle = .none
    win.minSize = NSSize(width: 780, height: 580)
    win.contentViewController = hosting
    win.isReleasedWhenClosed = false
    win.delegate = self
    win.center()
    window = win
  }

  func windowShouldClose(_ sender: NSWindow) -> Bool {
    settingsViewModel.flushPendingCommits()
    settingsViewModel.setWindowVisible(false)
    if allowTerminateClose {
      return true
    }
    sender.orderOut(nil)
    // 关闭设置窗口后回到安静菜单栏常驻态，不占 Cmd+Tab 与 Dock。
    NSApp.setActivationPolicy(.accessory)
    return false
  }

  func windowWillMiniaturize(_ notification: Notification) {
    settingsViewModel.setWindowVisible(false)
  }

  func windowDidDeminiaturize(_ notification: Notification) {
    settingsViewModel.setWindowVisible(true)
  }
}

@MainActor
enum MainMenuBuilder {
  static func install(onOpenSettings: @escaping () -> Void, onQuit: @escaping () -> Void) {
    let mainMenu = NSMenu()

    // 应用菜单
    let appMenuItem = NSMenuItem()
    let appMenu = NSMenu()
    let settingsItem = MenuActionItem(title: "设置…", keyEquivalent: ",", handler: onOpenSettings)
    appMenu.addItem(settingsItem)
    appMenu.addItem(.separator())
    let closeItem = NSMenuItem(
      title: "关闭窗口",
      action: #selector(NSWindow.performClose(_:)),
      keyEquivalent: "w"
    )
    appMenu.addItem(closeItem)
    appMenu.addItem(.separator())
    let quitItem = MenuActionItem(
      title: "退出 It's Class Time!",
      keyEquivalent: "q",
      handler: onQuit
    )
    appMenu.addItem(quitItem)
    appMenuItem.submenu = appMenu
    mainMenu.addItem(appMenuItem)

    // 编辑菜单（确保 SwiftUI TextField 支持 Cmd+X/C/V/A/Z）
    let editMenuItem = NSMenuItem()
    let editMenu = NSMenu(title: "编辑")
    editMenu.addItem(withTitle: "撤销", action: Selector(("undo:")), keyEquivalent: "z")
    let redo = NSMenuItem(title: "重做", action: Selector(("redo:")), keyEquivalent: "Z")
    redo.keyEquivalentModifierMask = [.command, .shift]
    editMenu.addItem(redo)
    editMenu.addItem(.separator())
    editMenu.addItem(withTitle: "剪切", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
    editMenu.addItem(withTitle: "复制", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
    editMenu.addItem(withTitle: "粘贴", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
    editMenu.addItem(withTitle: "全选", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
    editMenuItem.submenu = editMenu
    mainMenu.addItem(editMenuItem)

    NSApp.mainMenu = mainMenu
  }
}

private final class MenuActionItem: NSMenuItem {
  private let handler: () -> Void

  init(title: String, keyEquivalent: String, handler: @escaping () -> Void) {
    self.handler = handler
    super.init(title: title, action: #selector(invoke), keyEquivalent: keyEquivalent)
    self.target = self
    if !keyEquivalent.isEmpty {
      self.keyEquivalentModifierMask = [.command]
    }
  }

  @available(*, unavailable)
  required init(coder: NSCoder) {
    fatalError("init(coder:) has not been implemented")
  }

  @objc private func invoke() {
    handler()
  }
}
