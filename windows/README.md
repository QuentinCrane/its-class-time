# Windows 端｜C++/WinRT + WinUI 3 + Win32

Windows x64 发行包面向 Windows 10 1809 及更新版本。开发与桌面验收以 Windows 11 为主。

## 当前实现

| 能力 | 当前源码与验收 |
| --- | --- |
| 作息引擎 | C++17 实现。共享向量、配置兼容、时间轴与组件状态共 270 项检查通过。 |
| 设置 | 左侧九页导航。已在 100%、125%、200% 缩放下核对键盘切页、共用实时预览、作息范围和草稿恢复。 |
| 桌面组件 | 通过实机查看分节长条、圆环和屏幕边框，以及四边吸附、悬停和多显示器定位。 |
| 后台与通知 | 托盘支持键盘打开设置。安装版已核对组件隐藏时的合并通知、暂停与恢复及系统权限切换。 |
| 本地数据与安装 | 已核对覆盖安装保留课表、卸载保留数据及损坏配置备份恢复。 |

当前公开安装包为 v0.1.2。九页设置、重复星期快捷项、滚动选时、EXE 登录启动与通知权限复核属于之后的源码更新。构建与实机记录见 [验收记录](../docs/ACCEPTANCE.md)。

## 构建工具链

| 依赖 | 仓库使用的版本 |
| --- | --- |
| Visual Studio 或 Build Tools | 2022 17.8+，安装「使用 C++ 的桌面开发」 |
| Windows SDK | 10.0.22621.0 或更新版本 |
| Windows App SDK | 1.6.250602001，Windows App Runtime 对应为 1.6 |
| C++/WinRT | 2.0.240111.5 |
| Windows SDK BuildTools | 10.0.22621.756 |
| Inno Setup | 7，构建工作流使用 7.1.1 |

### 解包构建与调试

在仓库根目录还原固定的 NuGet 依赖。按照 Windows 工作流中的「Generate C++/WinRT projection headers」步骤生成投影到 `windows/Generated Files`，再运行构建脚本。 [Windows 构建工作流](../.github/workflows/build-windows.yml)

```powershell
nuget restore windows\packages.config -OutputDirectory windows\packages
# 完成 C++/WinRT 投影头文件生成后构建
.\windows\tools\build-unpackaged.cmd
```

脚本调用 `cl`、`link` 和 `rc`，产物为 `windows\build\app\ClassTimeApp.exe`。解包运行需要已安装 Windows App Runtime 1.6。首次运行前，将 Bootstrap DLL 和预设放在程序旁，并使用隔离目录调试。

```powershell
$buildApp = 'windows\build\app'
$bootstrap = 'windows\packages\Microsoft.WindowsAppSDK.1.6.250602001\runtimes\win-x64\native\Microsoft.WindowsAppRuntime.Bootstrap.dll'
Copy-Item -LiteralPath $bootstrap -Destination $buildApp
New-Item -ItemType Directory -Force -Path "$buildApp\presets" | Out-Null
Copy-Item -Path 'shared\presets\*.json' -Destination "$buildApp\presets"

$env:CLASSTIME_DATA_DIR = Join-Path $env:TEMP 'classtime-dev'
$env:CLASSTIME_SHOW_SETTINGS = '1'
.\windows\build\app\ClassTimeApp.exe
```

`tools/screenshot.cpp` 可单独编译，用于捕获全屏或指定标题的窗口。

### Windows x64 离线安装包

在仓库根目录运行。Inno Setup 装在自定义目录时传入 `-Compiler`。

```powershell
.\windows\tools\build-installer.ps1
# Inno Setup 安装在自定义目录时
.\windows\tools\build-installer.ps1 -Compiler 'C:\tools\Inno Setup 7\ISCC.exe'
```

安装脚本复用 `build\app\ClassTimeApp.exe`，将两份预设和 Windows App Runtime 1.6.9 打入 `windows\build\release\ItsClassTime-<版本>-Windows-x64-Setup.exe`。首次构建下载并校验运行时的 SHA-256，后续使用缓存。构建前从托盘退出运行中的应用。

安装适用于当前用户，默认路径为 `%LOCALAPPDATA%\Programs\ClassTime`。安装向导可选桌面快捷方式，开始菜单项随安装创建。覆盖安装沿用原路径。卸载可从「设置 → 应用 → 已安装的应用」进入，个人课表与共享运行时会保留。当前 EXE 未签名，SmartScreen 可能显示提示。

### MSIX 工程

仓库保留 `ClassTime.sln`、`ClassTime.vcxproj` 和 `Package.appxmanifest`。MSIX 的构建、部署和安装待验证。

```powershell
nuget restore windows\packages.config -OutputDirectory windows\packages
msbuild windows\ClassTime.sln /p:Configuration=Debug /p:Platform=x64 /restore
```

## 设置与进程

- 「作息计划」可切换常规模板与今天的计划。常规模板提供重复星期快捷项和逐日点选。点击时段起止时间可用 24 小时滚轮或键盘输入。
- 「时间轴」与「课表预览」沿用所选计划范围。时间轴支持 1、5、15 分钟步进调整，冲突时提示并恢复原始时段。「模板管理」可重命名模板、复制现有模板或新建空白模板。
- 「显示与位置」配置视图、范围、显示器和贴边行为。「组件外观」配置长条尺寸、背景色、边框与阶段色。两个页面共用固定的实时预览。
- 「通知」显示提醒状态与系统权限。「视觉提醒」单独配置边缘柔光、放大时钟及其外观。登录启动和退出在「常规」。
- 关闭设置后应用继续常驻。选择「退出」结束提醒和组件进程。登录启动使用当前用户的 Run 项，MSIX 工程对应 `StartupTask`。

本地配置在 `%LOCALAPPDATA%\ClassTime\config.json`。MSIX 形态使用对应的 `LocalState`，调试时可用 `CLASSTIME_DATA_DIR` 覆盖。发现损坏 JSON 或不兼容版本时，应用备份为 `config.json.corrupt-<时间>`，再恢复预设。同一秒已有备份时使用独立文件名。读写或备份失败时显示提示并停止启动。

## 实现索引

| 路径 | 作用 |
| --- | --- |
| `app/desktop_app.cpp` | 生命周期、单实例、装配桌面组件 |
| `app/settings_window.cpp` | WinUI 3 设置界面与编辑器 |
| `app/overlay_window.cpp` | HWND 组件、贴边、多屏与显示状态 |
| `app/tray_icon.cpp` | 托盘图标、键盘入口与菜单 |
| `app/services.cpp` | 本地配置、今日覆盖、作息快照 |
| `app/boundary_monitor.cpp` | 下一边界等待、过期跳过与事件去重 |
| `app/notification_service.cpp` · `app/visual_alert.cpp` | 系统即时通知与可选视觉效果 |
| `engine/` · `tests/engine_tests.cpp` | C++17 引擎、共享向量与配置回归 |
| `tools/` · `installer/` | 解包构建与离线安装向导 |

纯代码 WinUI 3 由 `DesktopApp` 提供 `IXamlMetadataProvider`，转发给 `XamlControlsXamlMetaDataProvider` 以解析 WinUI 默认控件样式。 [WinUI 3 资源加载记录](https://github.com/microsoft/microsoft-ui-xaml/issues/7606)

## 验证

在 x64 MSVC 开发终端从仓库根目录运行引擎检查。

```powershell
cmake -S windows -B windows/build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build/engine
.\windows\build\engine\engine_tests.exe (Get-Location).Path
```

- 关掉设置，从托盘用鼠标或键盘重开。切换长条、圆环与边框，拖动检查吸附与悬停详情。
- 在「作息计划」用快捷项和逐日点选修改重复星期，再到课表预览核对。用滚轮输入精确起止时间，在时间轴调整今日计划，再核对常规模板。
- 对安装版使用隔离的短课表，验证同一边界合并为一条系统通知，再核对暂停、恢复与系统权限切换。用「视觉提醒」的测试入口核对时钟悬停与收回。
- 在 100%、125%、200% 缩放中切换九个页面，核对焦点、预览与保存。独立测试机继续覆盖干净系统、休眠、物理拔插、Explorer 重启与系统无障碍场景。 [实机记录与完整用例](../docs/ACCEPTANCE.md)
