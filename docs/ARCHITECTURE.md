# ARCHITECTURE｜双端纯原生最小架构

> 目标：**单一常驻应用进程 + 原生浮窗 + 简单的作息引擎 + 即时通知适配器**。不要为了两端一致额外做跨语言共享运行库。
>
> **这份文档回答**：两端各用哪些系统 API、进程与线程怎么分工、配置存在哪、哪些“看起来合理”的抽象是被刻意拒绝的。双端实现进度与构建方式分别见 [windows/README.md](../windows/README.md) 与 [macos/README.md](../macos/README.md)。

## 1. 总体架构

```text
GitHub 单仓库（共享：需求/JSON Schema/预设/测试向量）
├── macos/   [SwiftUI + AppKit]
│   └── ItsClassTime.app            # 显示名 It's Class Time!
│       ├── ScheduleEngine + BoundaryMonitor (Swift)
│       ├── OverlayPanel (NSPanel + SwiftUI)
│       ├── MenuBar (NSStatusItem / SwiftUI menu)
│       ├── Preferences (SwiftUI Settings)
│       ├── NotificationAdapter (UserNotifications)
│       └── ConfigStore (Codable / Foundation)
└── windows/ [C++20 + C++/WinRT + WinUI 3 + Win32]
    └── ClassTimeApp.exe            # 工程标识沿用 ClassTime
        ├── ScheduleEngine + BoundaryMonitor (C++)
        ├── OverlayWindow (WinUI 3 + HWND/Win32)
        ├── TrayIcon (Shell_NotifyIcon)
        ├── PreferencesWindow (WinUI 3，代码构建 UI)
        ├── NotificationAdapter (AppNotificationManager)
        └── ConfigStore (仓库内自带的极简 JSON 读写，无第三方依赖)
```

**单进程**指每个系统上的 App 各自单进程；不是一个二进制跨平台。无需 Windows Service、macOS LaunchDaemon、后台网络服务、额外 IPC、跨平台 GUI、托管 .NET。

## 2. macOS 实现映射

| 能力 | 首选框架/API | 备注 |
|---|---|---|
| 用户可见设置 | SwiftUI `Settings`/Views | 遵循系统设置习惯 |
| 菜单栏 | `NSStatusItem` 或原生 SwiftUI 菜单栏接口 | 进程常驻控制与退出 |
| 无抢焦点的浮动条 | AppKit `NSPanel` / `NSWindow` + SwiftUI hosting | 测试不激活、浮动级别、鼠标 hover |
| 多显示器和 Spaces | `NSScreen`、`NSWindow.CollectionBehavior` | `canJoinAllSpaces` 等仅在适用时使用；不保证所有全屏 |
| 即时系统通知 | `UNUserNotificationCenter.add`，`UNNotificationRequest(trigger: nil)` | 先请求通知权限，遵守用户设置 |
| 时间与配置 | `Foundation`、`Codable`、本地文件 | 无数据库 |
| 睡眠/唤醒与时钟变化 | NSWorkspace / 系统通知与前台恢复核查 | 收到事件后重算，不能承诺睡眠中运行 |

## 3. Windows 实现映射

| 能力 | 首选框架/API | 备注 |
|---|---|---|
| 用户可见设置 | **C++/WinRT + WinUI 3**（代码构建 UI） | WinUI 控件，不是 C#；不依赖 XAML 编译器 |
| 悬浮条窗口 | WinUI 3 窗口取得 `HWND`，必要时调用 Win32 | 窗口位置、激活行为、DPI、置顶 |
| 置顶 | `SetWindowPos(..., HWND_TOPMOST, ...)` | 不保证覆盖安全桌面/独占全屏 |
| 托盘 | `Shell_NotifyIcon` | 正确处理 explorer 重启后的图标重建 |
| 即时系统通知 | `AppNotificationManager::Show` | 需要配套注册与应用标识/打包处理 |
| 单实例 | `AppInstance::FindOrRegisterForKey` | 防重复后台计时 |
| 休眠/唤醒 | Win32 电源事件/会话及系统时钟检查 | 唤醒仅重新定位最新边界 |
| 本地 JSON | 仓库内 `engine/src/json.cpp`（约 500 行） | 只实现项目所需子集，避免为一份配置引入第三方库；字段一致性由 `shared/schemas` 约束 |

### Windows 打包决策（已落地，附带代价）

- 最初计划使用官方 C++ WinUI 3 packaged 模板，以减少通知标识与部署配置的变量。
- 开发机上的 `MSBuild.exe` 被安全策略禁用，于是工程改为**纯代码构建 + 解包运行**：
  `tools/build-unpackaged.cmd` 直接调用 `cl`/`link`，运行时由 Bootstrapper 初始化 Windows App Runtime。
  这条路径让界面与引擎能被真实构建、截图核对，但通知注册、开机启动任务等打包相关能力只能留到打包后验证。
- 对外分发暂用 **Inno Setup 离线安装包**（`tools/build-installer.ps1`），把应用、预设与
  Windows App Runtime 一起打包，目标机器可离线安装。当前安装包未签名。
- `Package.appxmanifest` 与 MSIX 打包配置保留在仓库里，但**尚未构建验证**。不因为工程里存在清单就宣称支持 MSIX 发行。
- packaged / unpackaged / installer 三条路径的能力并不等价，**不要在未验证前互相替代宣称**。

## 4. 时间引擎（各端各写一份）

只维护有限职责：

```text
EffectivePlan(date) = todayOverride(date) ?? activeTemplate(date.weekday)
CurrentSegment(now) = 匹配 [start,end) 的已排序段或 idle
Progress(now,segment) = clamp((now-start)/(end-start),0,1)
NextBoundary(now) = 所有未来 start/end 中最早的一个
```

- 后台提醒使用**距离下一边界的等待**，到点后再核对墙上时间；另设低频看门狗兜底系统时间跳变与时钟回调丢失。配置、时区、时间/睡眠状态改变时中断等待重新计算。
- 浮窗定期绘制进度是**表现层刷新**，不决定是否发通知。将“显示 tick”与“提醒调度”分开，避免窗口隐藏导致通知失效。
- 不以 60FPS 循环驱动作息；如数值进度需要秒级显示，最多按必要频率局部更新，窗口隐藏时可停止 UI 更新。
- 采用稳定 ID 和有效计划修订编号防重复；事件键保留短期数据，不需要完整日志系统。

## 5. 配置存储

- 两端启动时先读取随应用打包的 `shared/presets`（或构建时复制）；用户修改写入平台标准用户应用数据目录。Windows 当前为 `%LOCALAPPDATA%\ClassTime`（打包形态为 `%LOCALAPPDATA%\Packages\<包族名>\LocalState`），macOS 为 `~/Library/Application Support/ClassTime`，双端均可用 `CLASSTIME_DATA_DIR` 覆盖以便隔离测试。
- 推荐最小文件：`config.json` 和可选 `runtime-state.json`（近期已发事件 ID）。
- 写入必须临时文件+原子替换；无效 JSON 或 schema 版本不兼容时**备份原文件并安全降级**。
- 预设样例和 schema 是跨端共同契约；如果未来改字段，要同时修改 schema、双端解析、测试向量和迁移说明。

## 6. 防过度工程化硬限制

不做独立进程定时服务、微服务、IPC、UI 通用框架、跨语言核心、消息队列、插件系统、SQLite、复杂 MVVM 框架库、反射驱动的资源自动注册。SwiftUI/WinUI 自有的基础状态管理是正常使用，不属于额外大型架构。
