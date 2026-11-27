# macOS 端｜Swift + SwiftUI + AppKit

> 目标平台：macOS 13 Ventura 及以上（Apple Silicon 原生构建，也可在 Intel Mac 从源码编译）。
> 工程类型：Swift Package + 原生 `ItsClassTime.app` 应用包与离线 `.dmg` 安装镜像（无需依赖完整 `Xcode.app`，仅安装 Command Line Tools 即可构建、测试与打包）。

## 1. 当前实现状态（2026-11-27，v0.1.2）

| 部分 | 状态 | 说明 |
|---|---|---|
| 作息引擎 `Sources/ClassTimeEngine/` | **已编译并测试** | 纯 `Foundation`，零 UI 依赖；`engine-tests` 220 项检查全部通过（含 `shared/test-cases/time-engine.json` 含 `scopeCases`、预设、严格 `schemaVersion` 校验、配置往返、自定义模板、时间轴拖动、组件状态与屏幕边框全天分色，0 失败） |
| 配置/今日覆盖/去重 | **已编译并冒烟通过** | 原子写入（`.atomic`）、不兼容版本拒绝、损坏配置不覆写已有备份（`.corrupt-<时间>` 同秒自增后缀）、只读/不可写目录拒绝覆写并停止启动、跨日清理、事件键去重 |
| 悬浮层 `OverlayController` + `OverlayViews` | **已编译打包** | `NSPanel`（`.borderless` + `.nonactivatingPanel` + `.floating` + `.canJoinAllSpaces`）+ macOS 26+ 原生 `NSGlassEffectView`（macOS 27+ 启用 `effectIsInteractive`）；分节长条（四边吸附、自定义长度 240–1600 pt、原生取色器自定义背景色）、圆环表盘、整屏四边进度环（贴合 `NSScreen.frame`、`.statusBar` 层级完全点击穿透、支持全天按时段分色/跟随当前时段）、多显示器选择与主屏（`NSScreen.screens.first`）回退、贴边自动收起把手、三击归中、悬停向屏幕内侧展开详情卡，以及长条/圆环/屏幕边框间的无闪断平滑过渡 |
| 设置窗口 `SettingsWindowController` + `SettingsView` + `ScheduleTimelineView` + `DisplayPreviewViews` | **已编译打包** | 原生 `NavigationSplitView` 四栏设置（全高内容窗口、统一液态玻璃工具栏、滚动边缘软羽化）；作息页支持自定义模板新增/复制/空白创建与防抖重命名、分组时段列表、复制时段、可缩放 1/5/15 分钟步进拖动时间轴、周/日课表网格与今日概览；显示页顶部固定双栏实时预览（近景+缩放桌面，仅窗口可见且显示页激活时刷新）；通知页 4 态有效摘要与对齐开关行；常规页 `SMAppService.mainApp` 状态检测与批准引导；关闭窗口只隐藏不退出 |
| 双层时段编辑器 `SegmentEditorSheet` | **已编译打包** | 支持新增 / 复制 / 编辑 / 时间轴双击指定时刻新增；1 分钟整数输入框与 5 分钟步进滑块双向同步（手动精确分钟值绝不静默取整）、仅今天/永久模板隔离提示、`shift`/`keep` 顺延、上下文草稿变更对比与冲突行定位 |
| 顶部状态栏 `MenuBarController` + `MenuBarPopoverView` | **已编译打包** | `NSStatusItem` 实时动态进度弧模板图标 + 可选等宽剩余时间 / 节次与剩余时间显示；左键展开系统原生 Liquid Glass `NSPopover` 控制面板（自动测高），右键 / Control+点击弹出分级原生 Liquid Glass `NSMenu`（显示形式/显示范围/状态栏/通透度子菜单、打开设置与退出） |
| 即时通知 `NotificationService` | **源码就绪，待桌面手验** | `UNUserNotificationCenter`（`trigger: nil`）即时投递、同秒“下课+休息开始”合并单条通知；专注/勿扰表现待手工复核 |
| 边界等待 `BoundaryMonitor` | **已编译打包** | 按下一边界精确休眠 + 60 秒看门狗；监听 `NSWorkspace.didWakeNotification` / 时钟 / 时区 / 跨日变化重锚，30 秒外过期事件跳过 |
| 应用包与离线镜像 `tools/` | **可构建并校验签名** | 生成 `macos/build/app/ItsClassTime.app`（含 Retina `AppIcon.icns`、`vtool` SDK 校准与随包预设）及 `macos/build/release/ItsClassTime-<版本>-macOS-arm64.dmg`；当前为 ad-hoc 签名，未公证 |

## 2. 工具链说明：Command Line Tools 独立构建兼容

在仅安装 `/Library/Developer/CommandLineTools`（未安装完整 `Xcode.app`）的 macOS 27（Swift 6.4.2 / SDK 27.2）环境下：
1. `xcodebuild` 与 `XCTest.framework` 不可用，因此引擎测试采用纯 `Foundation` 可执行目标 `engine-tests`（与 Windows `engine_tests.exe` 对齐），直接读取仓库 `shared/` 数据运行。
2. macOS 27 Command Line Tools 的宿主插件目录未附带 `libSwiftUIMacros.dylib`（仅随完整 `Xcode.app` 分发），直接使用 `@State` 宏会触发宏插件缺失编译错误；因此 SwiftUI 视图状态统一采用标准 `ObservableObject` + `@ObservedObject`，在 Command Line Tools 与完整 Xcode 下均可零警告直接编译。
3. SwiftPM 默认产物会将 Mach-O `LC_BUILD_VERSION` 中的 `sdk` 标记为最低系统版本（`13.0`），导致 macOS 26/27 将应用回退为旧版兼容外观。`build-app.sh` 会在签名前通过苹果官方 `vtool -set-build-version macos 13.0 <SDK_VERSION>` 校准链接 SDK 版本，同时保留 macOS 13+ 兼容性与完整 Liquid Glass 外观。

## 3. 构建与打包

### 单独运行作息引擎对等测试

```bash
swift run --package-path macos engine-tests "$(pwd)"
```

### 构建原生应用包 `ItsClassTime.app`

```bash
./macos/tools/build-app.sh
# 产物：macos/build/app/ItsClassTime.app
```

隔离数据目录调试运行（避免污染正式配置）：

```bash
CLASSTIME_DATA_DIR="/tmp/classtime-dev" CLASSTIME_SHOW_SETTINGS=1 \
  ./macos/build/app/ItsClassTime.app/Contents/MacOS/ItsClassTime
```

### 构建离线拖拽安装镜像 `.dmg`

```bash
./macos/tools/build-dmg.sh
# 产物：macos/build/release/ItsClassTime-<版本>-macOS-arm64.dmg
```

脚本会先执行 220 项共享与对等引擎检查，再以 Release 模式编译并组装 `ItsClassTime.app`（复制 `shared/presets/*.json`、生成多尺寸 `AppIcon.icns`、校准 `LC_BUILD_VERSION` SDK、执行 `codesign --sign -` 校验），最后通过 `hdiutil` 制作带 `Applications` 快捷方式的压缩镜像。

用户配置默认保存在 `~/Library/Application Support/ClassTime/config.json`，运行态通知去重键保存在同目录 `runtime-state.json`。升级覆盖或删除 `.app` 不会清除个人课表。

## 4. 目录结构

```text
macos/
├── Package.swift
├── Resources/
│   └── Info.plist                   # 应用标识、LSUIElement 菜单栏常驻、高分屏声明
├── Sources/
│   ├── ClassTimeEngine/             # 纯 Foundation 作息引擎（与 Windows engine 规则严格对等）
│   │   ├── Schedule.swift
│   │   ├── TimeEngine.swift
│   │   ├── EditOps.swift
│   │   ├── Config.swift
│   │   ├── TemplateOps.swift        # 自定义模板创建（复制当前 / 空白模板）
│   │   ├── TimelineEdit.swift       # 时间轴拖动步进与边界约束（仅对拖动量吸附）
│   │   └── WidgetState.swift        # 悬浮组件与预览共享状态、全天分色边框分段与平滑过渡点
│   └── ItsClassTime/                # SwiftUI + AppKit 原生桌面端
│       ├── Main.swift               # 入口、单实例文件锁与唤醒、启动配置安全报错、睡眠/时钟监听
│       ├── DisplayHelper.swift      # NSScreen.screens.first 主屏判定、多显示器枚举、Hex 与渐变配色
│       ├── LiquidGlassSupport.swift # macOS 26+/27+ NSGlassEffectView 与 SwiftUI 液态玻璃适配层
│       ├── Services.swift           # 配置原子写入、坏配置不覆写备份、模板管理、SMAppService 登录启动
│       ├── BoundaryMonitor.swift    # 下一边界等待线程与去重
│       ├── NotificationService.swift# UNUserNotificationCenter (trigger: nil) 即时通知与文案合并
│       ├── MenuBarController.swift  # NSStatusItem 实时状态显示、Liquid Glass Popover 与分级 NSMenu
│       ├── MenuBarPopoverView.swift # 顶部状态栏液态玻璃控制面板（实时进度环、时间线与快捷控制）
│       ├── OverlayController.swift  # NSPanel + NSGlassEffectView 置顶不抢焦点、多屏、四边吸附、三击归中、屏幕边框
│       ├── OverlayViews.swift       # 长条（含自定义背景色）/圆环/收起把手/悬停详情卡/全天分色四边进度环视图
│       ├── ScheduleTimelineView.swift # 可缩放 1/5/15 分钟步进拖动时间轴与周/日课表预览网格
│       ├── DisplayPreviewViews.swift  # 显示与位置顶部固定实时预览（近景+缩放桌面）与三形态选择卡
│       ├── SettingsWindowController.swift
│       └── SettingsView.swift       # Liquid Glass 四栏设置界面、自定义模板弹窗与双层时段编辑工作台
├── Tests/
│   └── EngineTestsRunner/main.swift # 220 项共享测试向量、配置版本、时间轴与组件状态对等检查
└── tools/
    ├── generate-icon.swift          # 生成全尺寸 AppIcon.iconset
    ├── build-app.sh                 # 构建并签名 ItsClassTime.app
    └── build-dmg.sh                 # 打包离线安装镜像 .dmg
```

## 5. 系统手工测试步骤

1. **启动与常驻（M0 / T15）**：运行 `open macos/build/app/ItsClassTime.app`。首次启动弹出设置窗口，同时屏幕底边出现分节长条、顶部菜单栏出现时钟图标；点击左上角红叉关闭设置窗口，确认菜单栏图标与桌面悬浮条继续保留，且 Dock 图标自动收起。
2. **不抢焦点与悬停详情（T13 / T14）**：前置打开任意编辑器或浏览器保持输入光标，将鼠标移至屏幕边缘分节长条上，确认向屏幕内侧展开课程详情卡（名称、第几节、起止、已用/剩余、下一段），且前台应用焦点不被抢走。
3. **拖拽四边吸附与三击归中（T14 / T27 / T28）**：拖动悬浮条靠近顶/底/左/右任一边缘松手，确认贴左/右侧自动切换为竖向分节，贴顶/底为横向分节；拖偏后连续三击悬浮组件，确认沿当前边平滑回到居中。
4. **长条 / 圆环 / 屏幕边框、分色与多显示器（T11 / T12 / T22 / T30 / T31）**：在「设置 → 显示与位置」确认顶部实时预览随下方设置滚动保持可见；切换长条 / 圆环 / 屏幕边框（确认切到圆环直接呈现平滑淡入的圆形表盘，无圆角矩形闪烁或卡顿）、调整长条长度（240–1600 pt）与恢复默认、开关「自定义背景色」通过原生取色器选色、选择屏幕边框「按时段分色 / 跟随当前时段」及选择指定显示器，确认桌面真实渲染与预览同步更新。
5. **自定义模板、时间轴拖动、课表预览与精确分钟编辑（T05–T10 / T29）**：在「设置 → 作息计划」点击「新增模板」分别创建复制模板和空白模板并测试重命名与切换（确认内置模板才可「恢复默认…」且不覆盖已有今日调整）；展开「时间轴 · 拖动调整时段」测试缩放、1/5/15 分钟步进拖动时段与两端、冲突回弹以及双击空白新增；展开「课表预览」点击块进入编辑并测试「复制」时段；编辑午休数字框输入 `121` 分钟确认不被滑块取整，验证「仅今天」与「永久修改模板」隔离及冲突行定位。
6. **即时通知与暂停提醒（T16–T18 / T21）**：在「设置 → 通知」查看顶部状态摘要，点击「发送测试通知」，授权后确认弹出系统原生通知横幅；开启「临时暂停提醒」，确认进度条照常推进但边界不弹通知，菜单栏图标显示静音斜杠。
7. **单实例与安全配置恢复（T23 / T24）**：应用运行中再次执行 `open -n macos/build/app/ItsClassTime.app`，确认只唤醒现有实例而不产生双份菜单栏图标；在隔离数据目录破坏 `config.json` 或写入不兼容 `schemaVersion: 2` 后启动，确认不覆写已有备份并恢复预设；将损坏配置及目录设为只读后启动，确认弹窗提示并停止启动且原文件未被覆写。
