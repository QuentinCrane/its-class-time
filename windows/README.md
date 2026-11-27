# Windows 端｜C++/WinRT + WinUI 3 + Win32

> 目标平台：Windows 10 1809（17763）及以上，主目标 Windows 11。
> 工程类型：C++/WinRT WinUI 3 桌面应用（MSIX 工程配置保留，同时支持解包运行）。

## 1. 当前实现状态（2026-11-22）

| 部分 | 状态 | 说明 |
|---|---|---|
| 作息引擎 `engine/` | **已编译并测试** | 纯 C++17，无 WinRT 依赖；`engine_tests` 244 项检查全部通过（含 `shared/test-cases`、配置兼容性、时间轴拖动和组件状态，0 失败） |
| 配置/今日覆盖/去重 | **已编译并测试** | JSON 往返、原子写入、失效覆盖清理、事件键去重 |
| 悬浮层 `app/overlay_window.*` | **源码通过 + 实机渲染验证** | 分节长条/圆环/屏幕边框在解包模式实机渲染成功（截图核对）；四边吸附、左右竖向排列与状态配色为 2026-11-22 新增，待实机核对 |
| 设置窗口 `app/settings_window.*` | **实机渲染验证** | 纯代码构建 UI；NavigationView、模板选择、星期勾选、时段列表、编辑浮层全部正常显示 |
| 应用装配 `app/desktop_app.*` + `main.cpp` | **实机验证** | 解包模式启动稳定常驻；托盘图标出现；崩溃问题已修复（见 §2） |
| 托盘 `app/tray_icon.*` | **部分验证** | 图标曾在系统托盘显示；WinUI 菜单及命令已在隔离实例验证，实际托盘右键入口和 explorer 重启重建待复核 |
| 即时通知 `app/notification_service.*` | **源码通过，未实机验证** | `AppNotificationManager.Show` 需在实机核对授权与弹出 |
| 边界等待 `app/boundary_monitor.*` | **源码通过，未实机验证** | 按最近边界睡眠 + 60 秒看门狗；睡眠唤醒场景待测 |
| 离线安装包 `installer/` | **已构建并安装验证** | Inno Setup 7，含 Windows App Runtime 1.6.9；未签名，干净机待验 |
| MSIX 打包 | **未验证** | 本机 MSBuild 被安全策略禁用；`tools/build-unpackaged.cmd` 提供无 MSBuild 构建路径 |

## 2. 本轮关键修复：IXamlMetadataProvider

应用改为**纯代码构建 WinUI 3**（不依赖 XAML 编译器）后，启动设置窗口必崩
（`0xC000027B` stowed exception in `Microsoft.UI.Xaml.dll`，资源报错
`Cannot find a resource with the given key: AcrylicBackgroundFillColorDefaultBrush`）。

根因：`XamlControlsResources` 要求 Application 实现 `IXamlMetadataProvider`，
否则 WinUI 控件主题资源解析失败，所有需要默认样式的控件（NavigationView、
ToggleSwitch、ComboBox、NumberBox、InfoBar……）在布局期崩溃；只用
Grid/Border/TextBlock 等基础元素的悬浮层因此幸存。

修复：`DesktopApp` 以
`ApplicationT<DesktopApp, Markup::IXamlMetadataProvider>` 声明，三个接口方法
转发给 `XamlTypeInfo::XamlControlsXamlMetaDataProvider`（见 `app/desktop_app.h`）。
参见 microsoft-ui-xaml issue #7606 与 winui3-without-xaml 指南。

## 3. 依赖与版本

| 依赖 | 版本 | 备注 |
|---|---|---|
| Visual Studio | 2022 17.8+ | MSIX/正式构建需“使用 C++ 的桌面开发”+ C++ WinUI 工具 |
| Windows SDK | 10.0.22621.0 或更高 | `WindowsTargetPlatformVersion=10.0` |
| Windows App SDK | 1.6.250602001 | `packages.config` 固定；解包运行需安装对应 Runtime 1.6 |
| C++/WinRT | 2.0.240111.5 | NuGet 投影 |
| SDK BuildTools | 10.0.22621.756 | MakePri/Signtool 等 |

## 4. 构建

### 解包构建（无需 MSBuild / XAML 编译器）

```cmd
:: 在仓库 windows\ 目录下
tools\build-unpackaged.cmd
```

脚本直接调用 `cl`/`link`/`rc`（vcvarsall 环境），产物为
`build\app\ClassTimeApp.exe`（解包模式，经 Bootstrapper 初始化 Windows App
Runtime 1.6）。`Microsoft.WindowsAppRuntime.Bootstrap.dll` 需在 exe 旁
（解包开发时可从 `packages\Microsoft.WindowsAppSDK.1.6.250602001\runtimes\win-x64\native\` 复制）。

调试运行隔离数据目录：

```powershell
$env:CLASSTIME_DATA_DIR = "<某个测试目录>"; $env:CLASSTIME_SHOW_SETTINGS = "1"
.\build\app\ClassTimeApp.exe
```

`tools\screenshot.cpp`（编译见脚本）提供全屏/按窗口标题截屏，用于 UI 验证。

设置中的「显示与位置」可调整分节长条长度与背景色；可恢复原来的方向默认长度和系统背景。「作息计划」提供可缩放的时间轴，拖动时段或两端调整时间，步进可选 1、5 或 15 分钟，当前查看的常规模板与今日计划分别保存。拖动产生重叠时会提示并恢复，不会移动其他时段。

「显示与位置」的实时预览固定在页面上方，显示形式、显示范围、位置与行为设置在下方独立滚动；组件细节和屏幕位置均按示意尺寸缩放，随真实作息与设置更新。窄窗口时设置卡片纵向排列。预览刷新仅在设置窗口打开且该页面可见时运行，不参与提醒调度。

设置窗口按作息、显示、通知和常规分组；作息页直接显示模板的生效星期与分组时段，模板重命名、时间轴和课表预览可展开。提醒和显示选项使用对齐的开关行。常规页的退出按钮会完整停止常驻服务并保存配置。

在「作息计划」点击「新增模板」，填写名称后可复制当前模板或创建空白模板；新模板会立即选中，空白模板默认周一至周五生效。已有的今日临时调整继续优先于新模板。自定义模板可以像预设一样编辑和切换；恢复默认仅适用于随包的高中、大学预设。

### Windows x64 安装 EXE

还原本节的 NuGet 包并安装 Inno Setup 7 后，在仓库根目录运行：

```powershell
.\windows\tools\build-installer.ps1
# 如编译器在自定义位置：
.\windows\tools\build-installer.ps1 -Compiler 'C:\tools\Inno Setup 7\ISCC.exe'
```

脚本重新编译解包版，把应用、两份作息预设和 Microsoft Windows App Runtime 1.6.9
打入 `windows\build\release\ItsClassTime-<版本>-Windows-x64-Setup.exe`。首次构建下载并校验
运行时的 SHA-256，之后使用 `build\installer-tools\` 下的缓存。目标电脑可以离线安装。
向导源码和浅色、深色插画在 `installer/`，构建已使用 Inno Setup 7.1.1。

安装包构建与日常开发现在共用 `build\app\ClassTimeApp.exe` 这一份产物，不会再出现
“安装包是新的、build 里的 exe 还是旧的”。构建前请先从托盘退出正在运行的 ClassTime；
脚本检测到同名进程会直接提示，而不是抛一个链接器占用错误。

双击安装包，选择安装路径及是否添加桌面快捷方式。默认安装到
`%LOCALAPPDATA%\Programs\ClassTime`，适用于当前用户，并创建开始菜单快捷方式。
覆盖安装会沿用原路径。应用运行时，从托盘点「退出」后即可继续安装或卸载。
通过「设置 → 应用 → 已安装的应用」可以卸载。个人课表与设置保留在
`%LOCALAPPDATA%\ClassTime`，共享的 Windows App Runtime 也会保留。

向导文案会说明安装范围（当前用户、无需账号与联网、运行环境已内置），
以及课表与设置单独保存、升级和卸载都不会删除。安装位置页在覆盖安装时也会显示，
方便重新选择是否创建桌面快捷方式；运行环境安装失败时会给出可操作的重试提示。

用户看到的名称统一为 **“上课”时间到！**（英文 **It's Class Time!**）：
开始菜单、桌面快捷方式、设置窗口标题、通知与卸载条目都使用这个名称。
`ClassTime` 仅作为工程与目录标识保留（`ClassTime.sln`、`ClassTimeApp.exe`、
`%LOCALAPPDATA%\Programs\ClassTime`、配置目录），改名不影响这些路径。

本机在 Windows 11 x64、125% 缩放下核对了中文向导、含中文和空格的路径、快捷方式、
完成后启动、覆盖安装与卸载。当前构建未签名。最低系统门槛为 Windows 10 1809 x64，
仍需在没有预装 Windows App Runtime 的系统验证完整的首次部署。

### MSIX / Visual Studio

```powershell
nuget restore windows\packages.config -OutputDirectory windows\packages
msbuild windows\ClassTime.sln /p:Configuration=Debug /p:Platform=x64 /restore
```

XAML 文件已移除，工程不再需要 XAML 编译器；首次用 MSBuild 构建时仍需核对
MSIX 打包与 `module.g.cpp` 相关目标的差异。

首次启动会：从包内 `presets/*.json` 读取高中/大学示例；将用户模板写入数据目录
`config.json`（打包时为 `%LOCALAPPDATA%\Packages\<包族名>\LocalState`，解包时
为 `%LOCALAPPDATA%\ClassTime`，可用 `CLASSTIME_DATA_DIR` 覆盖）；注册通知与
托盘图标，显示悬浮层。配置损坏或版本不受支持时，先将原文件备份为
`config.json.corrupt-<时间>`，再回退预设；同一秒已有备份时会另取文件名。
如果无法安全读取、备份或写入配置，应用提示并停止启动，保留原文件或已创建的备份。

## 5. 目录

```
windows/
├── ClassTime.vcxproj            # WinUI 3 应用工程（MSIX 清单保留）
├── ClassTime.sln
├── packages.config              # NuGet 固定版本
├── Package.appxmanifest         # 应用标识、通知能力、开机启动任务
├── main.cpp                     # 入口：打包激活 / 解包 Bootstrapper 双模式
├── app/
│   ├── desktop_app.*            # 生命周期、单实例、托盘/通知/监控装配；IXamlMetadataProvider
│   ├── services.*               # 配置、今日覆盖、快照、持久化
│   ├── boundary_monitor.*       # 边界等待线程与去重
│   ├── notification_service.*   # 系统即时通知与合并文案
│   ├── visual_alert.*           # 可选视觉到点动效（四边柔光闪光 / 掉落 3D 时钟）
│   ├── display_utils.h          # 多显示器查找与全屏应用检测
│   ├── overlay_window.*         # 悬浮长条/圆环（代码构建 UI）
│   ├── settings_window.*        # 设置窗口（纯代码构建 UI，无 XAML）
│   └── tray_icon.*              # 托盘图标与菜单
├── engine/                      # 纯 C++17 作息引擎（可独立测试）
├── tests/engine_tests.cpp
├── tools/
│   ├── build-unpackaged.cmd     # cl+link 直构脚本（无 MSBuild）
│   └── screenshot.cpp           # 截屏验证工具
└── CMakeLists.txt               # 只构建引擎与测试，不参与打包
```

引擎单独验证：

```powershell
cmake -S windows -B windows/build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build/engine
windows\build\engine\engine_tests.exe "E:\path\to\repo"
```

## 6. 与文档的对应关系

- **时间语义**：半开区间 `[start,end)`、进度 clamp、下一边界、focus 序号；见 `engine/src/time_engine.cpp`。
- **今日覆盖 vs 模板**：`Services::ApplyScheduleEdit`；永久修改不会触碰 `dailyOverrides`，今日覆盖也不会写回模板。
- **编辑策略**：`engine/src/edit_ops.cpp` 的 `Shift`/`Keep`，冲突返回 `conflictIds`，UI 不静默移动时段。
- **通知**：`BoundaryMonitor` 只按最近边界唤醒；到点后以真实时钟复核，30 秒容差外跳过；`NotificationService` 合并同一秒事件。可选开启的视觉到点动效（`VisualAlert`：四边柔光闪光与掉落 3D 电子/机械时钟）共用同一边界组装逻辑，仅作系统通知补充，不抢焦点并遵守全屏隐藏与减弱动态效果。
- **暂停**：只置 `notifications.muted`，进度与边界判断继续。
- **悬浮窗**：`OverlappedPresenter` 去边框、`SetWindowPos(HWND_TOPMOST)`、`WS_EX_NOACTIVATE`；四边吸附，左右贴边切换为竖向分节。默认 `flushWithEdge=true`，贴边时 0 间距、靠边圆角削平并关闭 DWM 外框，可直接落在任务栏上沿；关闭后恢复 12 DIP 悬浮间距。不承诺覆盖独占全屏。
- **多显示器**：在「显示与位置」选择指定显示器或跟随系统主显示器；只改变组件所在位置，不更改系统主屏。已保存的显示器不可用时，组件定位回退到系统主屏。
- **单实例**：`AppInstance::FindOrRegisterForKey("ClassTime")`。
- **开机启动**：清单中的 `ClassTimeStartup` 启动任务，默认关闭，由用户在设置页开启。

## 7. 手工验收（后续重点）

1. 启动后关闭设置窗口：悬浮层仍在，托盘可恢复设置（T15）。
2. 用高中模板，等到某一节结束：应只出现一条合并通知（T16，通知未实机验证）。
3. 设置页暂停提醒：进度继续走，不再弹通知；恢复后不补发（T17/T18）。
4. 修改午休时长：数字框输入 121 分钟不被滑块取整；选择“顺延”后下午各节整体后移（T05/T06/T09）。
5. 选择“后续保持不变”且与后续冲突：保存被阻止并指出冲突行（T10）。
6. 今日覆盖后切换模板：明天恢复常规模板，今日仍按覆盖（T07/T08）。
7. 睡眠 2 小时唤醒：只显示当前状态，不连弹旧通知（T19）。
8. 启动第二个实例：不产生第二套计时/通知（T23）。
9. 在隔离数据目录中构造损坏的 `config.json`：启动后应保留备份并恢复预设（T24）；
   文件被独占或只读导致无法安全恢复时，应提示并停止启动。
10. 通知权限被关闭：悬浮层正常工作，设置页显示权限状态（T21）。
11. 在「作息计划」新增复制模板和空白模板，编辑后切换并重启，确认各模板独立保存；若今天已有临时调整，创建或切换模板后今天仍按临时计划执行。
12. 在「通知 → 视觉到点动效」分别开启「全屏边缘闪光提醒」与「掉落 3D 放大时钟提醒」，切换 `3D 电子数字 / 3D 机械表盘` 与 `顶部居中 / 右上角 / 左上角` 并点击「测试视觉提醒」，核对不抢焦点、悬停暂留与点击立即收回。

## 8. 已知限制 / 后续

- 四边吸附与左右竖向排列已接入，需要在 125%/150% 缩放下实机核对贴边位置、悬停卡方向与自动隐藏动画。
- 悬浮窗贴边定位：config 可保存 `edge/offset`，实机核对贴底 offsetY 行为与拖动吸附细节（本轮未逐一交互验证）。
- hover 详情、多显示器热插拔监听、通知实弹均待实机确认。
- MSIX 打包、签名、CI 依赖可运行 MSBuild 的环境。
- `XamlControlsResources` 纯代码加载是社区支持路径（issue #7606），正式 MSIX 构建后应复核默认样式完整性。
