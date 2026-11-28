# ARCHITECTURE｜双端原生架构

> Windows 与 macOS 各自以一个原生应用进程运行作息引擎、桌面组件、设置和即时通知。构建与平台实现见 [Windows](../windows/README.md) 和 [macOS](../macos/README.md)。

## 1. 总体架构

```text
GitHub 单仓库（共享 JSON Schema、预设、测试向量）
├── macos/   [SwiftUI + AppKit]
│   └── ItsClassTime.app            # 显示名 It's Class Time!
│       ├── TimeEngine + BoundaryMonitor (Swift)
│       ├── OverlayController (NSPanel + SwiftUI)
│       ├── MenuBarController (NSStatusItem / NSPopover / NSMenu)
│       ├── SettingsWindowController (SwiftUI NavigationSplitView)
│       ├── NotificationService (UserNotifications)
│       └── Services (Codable / Foundation)
└── windows/ [C++17 + C++/WinRT + WinUI 3 + Win32]
    └── ClassTimeApp.exe            # 工程标识沿用 ClassTime
        ├── TimeEngine + BoundaryMonitor (C++)
        ├── OverlayWindow (WinUI 3 + HWND/Win32)
        ├── TrayIcon (Shell_NotifyIconW)
        ├── SettingsWindow (WinUI 3，九页侧栏)
        ├── NotificationService (AppNotificationManager)
        └── Services (本地 JSON、有效作息与快照)
```

## 2. macOS 实现映射

| 能力 | 首选框架/API | 备注 |
|---|---|---|
| 用户可见设置 | SwiftUI `NavigationSplitView` | 作息、显示、通知和常规 |
| 菜单栏 | `NSStatusItem`、`NSPopover`、`NSMenu` | 实时状态、快捷控制与退出 |
| 无抢焦点的浮动条 | AppKit `NSPanel` / `NSWindow` + SwiftUI hosting | 测试不激活、浮动级别、鼠标 hover |
| 多显示器和 Spaces | `NSScreen`、`NSWindow.CollectionBehavior` | `canJoinAllSpaces` 等仅在适用时使用；不保证所有全屏 |
| 即时系统通知 | `UNUserNotificationCenter.add`，`UNNotificationRequest(trigger: nil)` | 先请求通知权限，遵守用户设置 |
| 时间与配置 | `Foundation`、`Codable`、本地 JSON | 配置和近期事件键分别保存 |
| 睡眠/唤醒与时钟变化 | NSWorkspace 与系统事件 | 收到事件后按当前时钟重算 |

## 3. Windows 实现映射

| 能力 | 首选框架/API | 备注 |
|---|---|---|
| 用户可见设置 | **C++/WinRT + WinUI 3**（代码构建 UI） | 九页侧栏，作息页共享范围，显示页共享预览 |
| 悬浮条窗口 | WinUI 3 窗口取得 `HWND`，必要时调用 Win32 | 窗口位置、激活行为、DPI、置顶 |
| 置顶 | `SetWindowPos(..., HWND_TOPMOST, ...)` | 不保证覆盖安全桌面/独占全屏 |
| 托盘 | `Shell_NotifyIconW`、`Shell_NotifyIconGetRect` | 响应鼠标和键盘，菜单锚定托盘图标，收到任务栏重建事件时恢复图标 |
| 即时系统通知 | `AppNotificationManager::Show` | 注册应用标识，投递前查询系统权限，返回通知页时刷新权限状态 |
| 单实例 | `AppInstance::FindOrRegisterForKey` | 防重复后台计时 |
| 休眠/唤醒 | Win32 电源事件/会话及系统时钟检查 | 唤醒仅重新定位最新边界 |
| 本地 JSON | 仓库内 `engine/src/json.cpp` | 字段由 `shared/schemas` 约束 |

### Windows 构建与分发

- 解包构建通过 `tools/build-unpackaged.cmd` 调用 `cl`、`link` 和 `rc`。启动时由 Bootstrapper 初始化 Windows App Runtime，应用通过 Windows App SDK 注册即时通知。
- Inno Setup 将同一份解包程序、预设和 Windows App Runtime 打入离线 EXE。用户启用登录启动时，应用登记当前用户的 Run 项。
- MSIX 工程使用 `Package.appxmanifest` 和 `StartupTask`。MSIX 构建与安装验收待完成。

## 4. 时间引擎（各端各写一份）

只维护有限职责：

```text
EffectivePlan(date) = todayOverride(date) ?? activeTemplate(date.weekday)
CurrentSegment(now) = 匹配 [start,end) 的已排序段或 idle
Progress(now,segment) = clamp((now-start)/(end-start),0,1)
NextBoundary(now) = 所有未来 start/end 中最早的一个
```

- 后台提醒使用**距离下一边界的等待**，到点后再核对墙上时间；另设低频看门狗兜底系统时间跳变与时钟回调丢失。配置、时区、时间/睡眠状态改变时中断等待重新计算。
- 浮窗按显示需要局部刷新，隐藏时可停止绘制。提醒继续按下一边界运行。
- 使用稳定 ID 和有效计划修订编号对事件去重，近期已触发的键单独保存。

## 5. 配置存储

- 两端启动时先读取随应用打包的 `shared/presets`（或构建时复制）；用户修改写入平台标准用户应用数据目录。Windows 当前为 `%LOCALAPPDATA%\ClassTime`（打包形态为 `%LOCALAPPDATA%\Packages\<包族名>\LocalState`），macOS 为 `~/Library/Application Support/ClassTime`，双端均可用 `CLASSTIME_DATA_DIR` 覆盖以便隔离测试。
- 推荐最小文件：`config.json` 和可选 `runtime-state.json`（近期已发事件 ID）。
- 写入必须临时文件+原子替换；无效 JSON 或 schema 版本不兼容时**备份原文件并安全降级**。
- 预设样例和 schema 是跨端共同契约；如果未来改字段，要同时修改 schema、双端解析、测试向量和迁移说明。

## 6. 状态管理

两个客户端分别使用 SwiftUI 和 WinUI 的状态管理能力。配置、提醒、桌面展示和设置各由应用进程内的模块负责，共享数据通过 JSON 契约对齐。
