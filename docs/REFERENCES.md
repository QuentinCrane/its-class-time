# REFERENCES｜官方开发资料

> 检索核对时间：2026-11-21，2026-11-22 补充纯代码 WinUI 与打包工具条目。下列链接以官方技术文档为准；SDK 新旧可能变化，实际开始工程时需重新看对应目标版本的 API 兼容性。不是所有官网示例都直接适用于最终项目打包类型。

## Apple

1. [SwiftUI Settings](https://developer.apple.com/documentation/swiftui/settings) — macOS 设置窗口。
2. [NSStatusItem](https://developer.apple.com/documentation/appkit/nsstatusitem) — 菜单栏常驻入口。
3. [NSWindow.level](https://developer.apple.com/documentation/appkit/nswindow/level-swift.property) — 浮动窗口层级。
4. [NSWindow.CollectionBehavior](https://developer.apple.com/documentation/appkit/nswindow/collectionbehavior-swift.struct) — Spaces/全屏相关行为与适用边界。
5. [UNUserNotificationCenter](https://developer.apple.com/documentation/usernotifications/unusernotificationcenter) — 系统通知权限与投递。
6. [UNUserNotificationCenter.add](https://developer.apple.com/documentation/usernotifications/unusernotificationcenter/add(_:withcompletionhandler:)) — `UNNotificationRequest(trigger: nil)` 可请求立即投递。

## Microsoft

1. [Get started with C++/WinRT](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/get-started) — C++/WinRT 基础。
2. [WinUI 3 C++ project templates](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/winui-project-templates-in-visual-studio) — 先确认**C++ 模板**而不是 C#。
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

## GitHub

1. [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) — macOS/Windows 构建环境。
2. [GitHub Releases documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases) — 单个 Release 下的双平台产物。

## 重要边界

- 原生通知由操作系统最终呈现，用户权限/勿扰模式可能不展示横幅；不能把概念原型中的通知卡片当成必须复现的 UI。
- macOS 全屏 Spaces 与 Windows 独占全屏并不承诺覆盖所有场景；不应依赖私有 API。
- `AppNotificationManager.Show` 是**立即通知**，不是 OS 定时排程；本项目通知时点由**当前运行的本机 App**判断。
