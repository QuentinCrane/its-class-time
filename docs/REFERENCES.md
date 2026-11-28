# REFERENCES｜官方开发资料

> 平台 API 与构建资料索引。项目使用的版本见双端构建指南。Windows 托盘键盘入口与通知权限资料更新于 2026-11-28。

## Apple

1. [SwiftUI Settings](https://developer.apple.com/documentation/swiftui/settings) — macOS 设置窗口。
2. [NSStatusItem](https://developer.apple.com/documentation/appkit/nsstatusitem) — 菜单栏常驻入口，以及 macOS 27 `expandedInterfaceDelegate` / `NSStatusItemExpandedInterfaceSession`。
3. [NSWindow.level](https://developer.apple.com/documentation/appkit/nswindow/level-swift.property) — 浮动窗口层级。
4. [NSWindow.CollectionBehavior](https://developer.apple.com/documentation/appkit/nswindow/collectionbehavior-swift.struct) — Spaces/全屏相关行为与适用边界。
5. [UNUserNotificationCenter](https://developer.apple.com/documentation/usernotifications/unusernotificationcenter) — 系统通知权限与投递。
6. [UNUserNotificationCenter.add](https://developer.apple.com/documentation/usernotifications/unusernotificationcenter/add(_:withcompletionhandler:)) — `UNNotificationRequest(trigger: nil)` 可请求立即投递。
7. [Adopting Liquid Glass](https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass) — macOS 26+ Liquid Glass 结构层级、移除不透明背景与系统无障碍适配规范。
8. [Applying Liquid Glass to custom views](https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views) — SwiftUI `.glassEffect(_:in:)`、`GlassEffectContainer`、`.buttonStyle(.glass)` 与 `.scrollEdgeEffectStyle(_:for:)`。
9. [NSGlassEffectView](https://developer.apple.com/documentation/appkit/nsglasseffectview) — AppKit 液态玻璃容器（macOS 26+ `cornerRadius`/`style`/`tintColor`，macOS 27+ `effectIsInteractive`）。
10. [HIG: Menu Bar Extras](https://developer.apple.com/design/human-interface-guidelines/the-menu-bar) — macOS 顶部状态栏模板图标、紧凑信息展示与用户可配置显示规范。
11. [NSScreen.screens](https://developer.apple.com/documentation/appkit/nsscreen/screens) — 系统主显示器固定为 `screens.first`（带菜单栏、原点 `(0,0)`），区别于随键盘焦点变化的 `NSScreen.main`；工作区避让菜单栏/刘海与 Dock 使用 `visibleFrame`。
12. [SMAppService](https://developer.apple.com/documentation/servicemanagement/smappservice) — macOS 13+ 登录启动项注册、状态查询（`.enabled` / `.requiresApproval`）与跳转系统登录项设置（`openSystemSettingsLoginItems()`）。
13. [SwiftUI ColorPicker](https://developer.apple.com/documentation/swiftui/colorpicker) — 接入 macOS 原生系统取色面板（`NSColorPanel`），支持 `supportsOpacity: false`。
14. [SpatialTapGesture](https://developer.apple.com/documentation/swiftui/spatialtapgesture) — macOS 13+ 捕获带坐标的双击手势，用于时间轴空白处双击新增时段。

## Microsoft

1. [Get started with C++/WinRT](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/get-started) — C++/WinRT 基础。
2. WinUI 3 的 C++ 桌面项目模板。 [WinUI 3 C++ project templates](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/winui-project-templates-in-visual-studio)
3. [Windows App SDK / WinUI](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/) — Windows 原生 SDK 主入口。
4. [App notifications quickstart (C++)](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/notifications/app-notifications/app-notifications-quickstart?tabs=cpp) — 即时发送/响应系统通知。
5. [AppNotificationManager.Show](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.windows.appnotifications.appnotificationmanager.show) — Windows App SDK 即时通知。
6. [Shell notification area](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area) — 系统托盘，`Shell_NotifyIcon`。
7. [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos) — Win32 置顶与位置。
8. [Windows App SDK desktop app lifecycle](https://learn.microsoft.com/en-us/windows/apps/develop/launch/app-lifecycle) — 普通桌面进程生命周期。
9. [Unpackaged Windows App SDK runtime](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/use-windows-app-sdk-run-time) — 解包运行的初始化差异（本项目当前使用这条路径）。
10. [microsoft-ui-xaml #7606](https://github.com/microsoft/microsoft-ui-xaml/issues/7606) — 纯代码构建 WinUI 3 时，`XamlControlsResources` 要求应用实现 `IXamlMetadataProvider`，否则控件默认样式解析失败。
11. [Inno Setup 帮助文档](https://jrsoftware.org/ishelp/) — 当前 Windows 离线安装包使用的打包工具。
12. [ScrollViewer.ChangeView](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.scrollviewer.changeview?view=windows-app-sdk-1.6) — 时间轴首次定位；2026-11-25 核对 Windows App SDK 1.6 支持该接口。
13. [FlyoutBase.ShouldConstrainToRootBounds](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.controls.primitives.flyoutbase.shouldconstraintorootbounds?view=windows-app-sdk-1.6) — 托盘菜单可延伸至锚点窗口之外；Windows App SDK 1.4 起支持，项目使用 1.6。
14. [GradientStop](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.media.gradientstop?view=windows-app-sdk-1.6) — WinUI 预览使用渐变色标表达边框的平滑颜色过渡。
15. 托盘用 `NOTIFYICON_VERSION_4` 接收键盘选中和菜单事件。 [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)
16. Windows 在投递通知前查询应用的系统通知权限。 [AppNotificationManager.Setting](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.windows.appnotifications.appnotificationmanager.setting?view=windows-app-sdk-1.8)

## GitHub

1. [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) — macOS/Windows 构建环境。
2. [GitHub Releases documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases) — 单个 Release 下的双平台产物。

## 系统行为

- 操作系统按照通知权限与勿扰模式呈现横幅。
- 浮窗层级遵循平台的窗口规则，包括 macOS 全屏 Spaces 与 Windows 独占全屏。
- 本机常驻应用计算下一作息边界，到点调用系统即时通知。
