# Windows 端｜C++/WinRT + WinUI 3 + Win32

> 目标平台：Windows 10 1809（17763）及以上，主目标 Windows 11。
> 工程类型：C++/WinRT WinUI 3 桌面应用（MSIX 工程配置保留，同时支持解包运行）。

## 1. 当前实现状态（2026-11-21 第二轮）

| 部分 | 状态 | 说明 |
|---|---|---|
| 作息引擎 `engine/` | **已编译并测试** | 纯 C++17，无 WinRT 依赖；`shared/test-cases/time-engine.json` 125 项检查全部通过 |
| 配置/今日覆盖/去重 | **已编译并测试** | JSON 往返、原子写入、失效覆盖清理、事件键去重 |
| 悬浮层 `app/overlay_window.*` | **源码通过 + 实机渲染验证** | 分节长条/圆环在解包模式实机渲染成功（截图核对） |
| 设置窗口 `app/settings_window.*` | **实机渲染验证** | 纯代码构建 UI；NavigationView、模板选择、星期勾选、时段列表、编辑浮层全部正常显示 |
| 应用装配 `app/desktop_app.*` + `main.cpp` | **实机验证** | 解包模式启动稳定常驻；托盘图标出现；崩溃问题已修复（见 §2） |
| 托盘 `app/tray_icon.*` | **实机验证** | 图标出现在系统托盘；explorer 重启重建逻辑未实测 |
| 即时通知 `app/notification_service.*` | **源码通过，未实机验证** | `AppNotificationManager.Show` 需在实机核对授权与弹出 |
| 边界等待 `app/boundary_monitor.*` | **源码通过，未实机验证** | 按最近边界睡眠 + 60 秒看门狗；睡眠唤醒场景待测 |
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
（脚本后手工复制或从 `packages\Microsoft.WindowsAppSDK.1.6.250602001\runtimes\win10-x64\native\` 获取）。

调试运行隔离数据目录：

```powershell
$env:CLASSTIME_DATA_DIR = "<某个测试目录>"; $env:CLASSTIME_SHOW_SETTINGS = "1"
.\build\app\ClassTimeApp.exe
```

`tools\screenshot.cpp`（编译见脚本）提供全屏/按窗口标题截屏，用于 UI 验证。

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
托盘图标，显示悬浮层。配置损坏时备份为 `config.corrupt-<时间>.json` 并回退预设。

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
- **通知**：`BoundaryMonitor` 只按最近边界唤醒；到点后以真实时钟复核，30 秒容差外跳过；`NotificationService` 合并同一秒事件。
- **暂停**：只置 `notifications.muted`，进度与边界判断继续。
- **悬浮窗**：`OverlappedPresenter` 去边框、`SetWindowPos(HWND_TOPMOST)`、`WS_EX_NOACTIVATE`；不承诺覆盖独占全屏。
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
9. 手工破坏 `config.json`：应备份并安全启动（T24）。
10. 通知权限被关闭：悬浮层正常工作，设置页显示权限状态（T21）。

## 8. 已知限制 / 后续

- 悬浮窗贴边定位：config 可保存 `edge/offset`，实机核对贴底 offsetY 行为与拖动吸附细节（本轮未逐一交互验证）。
- hover 详情、多显示器热插拔监听、通知实弹均待实机确认。
- MSIX 打包、签名、CI 依赖可运行 MSBuild 的环境。
- `XamlControlsResources` 纯代码加载是社区支持路径（issue #7606），正式 MSIX 构建后应复核默认样式完整性。
