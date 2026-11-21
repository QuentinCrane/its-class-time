# Windows 端｜C++/WinRT + WinUI 3 + Win32

> 目标平台：Windows 10 1809（17763）及以上，主目标 Windows 11。
> 工程类型：C++/WinRT WinUI 3 桌面应用，单项目 MSIX（packaged）。

## 1. 当前实现状态（2026-11-21）

| 部分 | 状态 | 说明 |
|---|---|---|
| 作息引擎 `engine/` | **已编译并测试** | 纯 C++17，无 WinRT 依赖；`shared/test-cases/time-engine.json` 125 项检查全部通过 |
| 配置/今日覆盖/去重 | **已编译并测试** | JSON 往返、原子写入、失效覆盖清理、事件键去重 |
| 悬浮层 `app/overlay_window.*` | **源码编译通过** | WinUI 3 内容 + Win32 置顶/不抢焦点；分节长条与圆环 |
| 托盘 `app/tray_icon.*` | **源码编译通过** | `Shell_NotifyIcon` + 原生菜单 + explorer 重启重建 |
| 即时通知 `app/notification_service.*` | **源码编译通过** | `AppNotificationManager.Show`，同一秒“下课+课间”合并为一条 |
| 边界等待 `app/boundary_monitor.*` | **源码编译通过** | 按最近边界睡眠 + 60 秒看门狗；启动/唤醒不补发过期事件 |
| 设置窗口 `MainWindow.xaml(.cpp)` | **代码编译通过（桩），XAML 未编译** | 本机 MSBuild 被安全策略禁止，XAML 代码生成无法运行 |
| `App.xaml.cpp` | **未编译验证** | 仅人工审查；依赖 XAML 生成的 `App.xaml.g.h` |
| MSIX 打包 / 实机运行 | **未验证** | 需要一台可运行 MSBuild 的 VS2022 环境 |

编译器验证方式：本机 VS2022 BuildTools 的 MSVC 19.44（v143）+ Windows SDK 22621 + Windows App SDK 1.6 的 C++/WinRT 投影，使用 `cl /c` 逐文件编译；`MainWindow.xaml.cpp` 使用自动生成的同名访问器桩头文件。**这不等于完整构建，也不代表 XAML 标记、链接和打包已经通过。**

## 2. 依赖与版本

| 依赖 | 版本 | 备注 |
|---|---|---|
| Visual Studio | 2022 17.8+ | 需“使用 C++ 的桌面开发”和 C++ WinUI 应用开发工具 |
| Windows SDK | 10.0.22621.0 或更高 | 工程 `WindowsTargetPlatformVersion=10.0`，自动选择已安装最高版本 |
| Windows App SDK | 1.6.250602001 | `packages.config` 固定；升级需同步核对通知/窗口 API |
| C++/WinRT | 2.0.240111.5 | 由 NuGet 提供投影与代码生成 |
| SDK BuildTools | 10.0.22621.756 | 提供 MakeAppx/Signtool 等 |

> 版本固定是为了可复现；升级 Windows App SDK 时请先对照 `docs/REFERENCES.md` 与官方模板，不要直接改版本号后宣称可用。

## 3. 构建

```powershell
# 在仓库根目录
nuget restore windows\packages.config -OutputDirectory windows\packages
# 或用 Visual Studio 打开 windows\ClassTime.sln，右键“还原 NuGet 包”

msbuild windows\ClassTime.sln /p:Configuration=Debug /p:Platform=x64 /restore
```

在 Visual Studio 中：打开 `windows/ClassTime.sln` → 选择 `x64` → 生成 → 部署（F5）。首次启动会：

1. 从包内 `presets/*.json` 读取高中/大学示例；
2. 将用户模板写入 `%LOCALAPPDATA%\Packages\<包族名>\LocalState\config.json`；
3. 注册通知与托盘图标，显示悬浮层。

配置损坏时会在同目录留下 `config.corrupt-<时间>.json` 并回退到随包预设。

## 4. 目录

```
windows/
├── ClassTime.vcxproj            # WinUI 3 应用工程（含 MSIX 清单）
├── ClassTime.sln
├── packages.config              # NuGet 固定版本
├── Package.appxmanifest         # 应用标识、通知能力、开机启动任务
├── App.xaml(.h/.cpp)            # 生命周期、单实例、托盘/通知/监控装配
├── MainWindow.xaml(.h/.cpp/.idl)# 设置窗口（作息/显示/通知/常规 + 编辑浮层）
├── app/
│   ├── services.*               # 配置、今日覆盖、快照、持久化
│   ├── boundary_monitor.*       # 边界等待线程与去重
│   ├── notification_service.*   # 系统即时通知与合并文案
│   ├── overlay_window.*         # 悬浮长条/圆环（代码构建 UI）
│   └── tray_icon.*              # 托盘图标与菜单
├── engine/                      # 纯 C++17 作息引擎（可独立测试）
├── tests/engine_tests.cpp
└── CMakeLists.txt               # 只构建引擎与测试，不参与打包
```

引擎单独验证：

```powershell
cmake -S windows -B windows/build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build/engine
windows\build\engine\engine_tests.exe "E:\path\to\repo"
```

## 5. 与文档的对应关系

- **时间语义**：半开区间 `[start,end)`、进度 clamp、下一边界、focus 序号；见 `engine/src/time_engine.cpp`。
- **今日覆盖 vs 模板**：`Services::ApplyScheduleEdit`；永久修改不会触碰 `dailyOverrides`，今日覆盖也不会写回模板。
- **编辑策略**：`engine/src/edit_ops.cpp` 的 `Shift`/`Keep`，冲突返回 `conflictIds`，UI 不静默移动时段。
- **通知**：`BoundaryMonitor` 只按最近边界唤醒；到点后以真实时钟复核，30 秒容差外跳过；`NotificationService` 合并同一秒事件。
- **暂停**：只置 `notifications.muted`，进度与边界判断继续。
- **悬浮窗**：`OverlappedPresenter` 去边框、`SetWindowPos(HWND_TOPMOST)`、`WS_EX_NOACTIVATE`；不承诺覆盖独占全屏。
- **单实例**：`AppInstance::FindOrRegisterForKey("ClassTime")`。
- **开机启动**：清单中的 `ClassTimeStartup` 启动任务，默认关闭，由用户在设置页开启。

## 6. 手工验收（本轮应重点检查）

1. 启动后关闭设置窗口：悬浮层仍在，托盘可恢复设置（T15）。
2. 用高中模板，等到某一节结束：应只出现一条合并通知（T16）。
3. 设置页暂停提醒：进度继续走，不再弹通知；恢复后不补发（T17/T18）。
4. 修改午休时长：数字框输入 121 分钟不被滑块取整；选择“顺延”后下午各节整体后移（T05/T06/T09）。
5. 选择“后续保持不变”且与第 5 节冲突：保存被阻止并指出冲突行（T10）。
6. 今日覆盖后切换模板：明天恢复常规模板，今日仍按覆盖（T07/T08）。
7. 睡眠 2 小时唤醒：只显示当前状态，不连弹旧通知（T19）。
8. 启动第二个实例：不产生第二套计时/通知（T23）。
9. 手工破坏 `config.json`：应备份并安全启动（T24）。
10. 通知权限被关闭：悬浮层正常工作，设置页显示权限状态（T21）。

## 7. 已知限制 / 后续

- 本轮未实现：左右竖向浮条、自定义铃声、主题切换、统计、跨设备同步（均属后续功能池）。
- 多显示器热插拔时窗口回到可见区域：目前按“当前显示器工作区”重新定位，尚未监听显示器变化事件。
- hover 详情使用 WinUI `ToolTipService`，在不抢焦点的悬浮窗上需实机确认弹出行为。
- `App.xaml.cpp` 与 XAML 标记尚未经过真实 MSBuild/XAML 编译；首次在 VS2022 构建时请优先修复工程级问题，不要据此认为已可发布。
