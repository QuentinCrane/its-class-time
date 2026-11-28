# macOS 端｜Swift + SwiftUI + AppKit

v0.1.2 提供 Apple Silicon 离线安装镜像。应用支持 macOS 13 及以上，macOS 26 起使用系统 Liquid Glass 外观。 [发行页面](https://github.com/QuentinCrane/its-class-time/releases/tag/v0.1.2)

## 桌面与设置

| 入口 | 用法 |
| --- | --- |
| **作息计划** | 选择、复制或新建模板，编辑常规模板或今天的计划。展开时间轴可缩放、拖动时段和调整两端，步进可选 1、5、15 分钟。周/日课表可预览并打开时段编辑。 |
| **显示与位置** | 近景与屏幕位置预览固定在顶部。可切换三种视图及显示范围、选择显示器，并调整长条尺寸、背景和边框配色。 |
| **通知** | 查看权限与提醒状态，设置触发时机，暂停提醒或发送测试通知。 |
| **常规** | 管理登录启动、查看数据目录和版本、退出应用。 |
| **菜单栏** | 左键打开控制面板，右键或 Control+点击打开菜单。菜单栏可显示当前剩余时间或节次与剩余时间。关闭设置后从这里控制组件与提醒。 |

分节长条与圆环支持四边吸附、收起把手、悬停详情和三击归中。屏幕边框贴合目标显示器的完整屏幕，点击可穿过边框。全天可按各段实际时长分色。

## 构建与打包

从仓库根目录运行。开发机使用提供 macOS 27 SDK 的 Command Line Tools 或 Xcode，产物按构建机架构生成。

```bash
# 单独执行作息引擎检查
swift run --package-path macos engine-tests "$(pwd)"

# 构建应用包
./macos/tools/build-app.sh

# 构建应用并制作离线镜像
./macos/tools/build-dmg.sh
```

产物在 `macos/build/app/ItsClassTime.app` 和 `macos/build/release/ItsClassTime-<版本>-macOS-<架构>.dmg`。构建脚本运行引擎检查，组装 Retina 图标和随包预设，校准 Mach-O 的 SDK 标记，再执行 Ad-hoc 签名和完整性校验。离线镜像包含「应用程序」快捷方式。

macOS 27 的 Command Line Tools 路径曾用 Swift 6.4.2 / SDK 27.2 验证。引擎检查采用 Foundation 可执行目标，界面用 `ObservableObject` 和 `@ObservedObject` 管理视图状态，适配 Command Line Tools 与完整 Xcode。构建脚本通过 `vtool` 保持最低部署版本为 13.0，并用当前 SDK 版本标记启用适用的系统外观。

安装时将 `ItsClassTime.app` 拖入「应用程序」。当前应用采用 Ad-hoc 签名，Apple 公证待完成。个人课表和设置保存在 `~/Library/Application Support/ClassTime/config.json`，近期通知事件键保存在同目录的 `runtime-state.json`。

## 实现索引

| 路径 | 作用 |
| --- | --- |
| `Sources/ClassTimeEngine/` | 时间引擎、JSON 配置、模板、时间轴和组件状态 |
| `Tests/EngineTestsRunner/main.swift` | 共享向量、配置往返、时间轴与显示规则检查 |
| `Sources/ItsClassTime/Main.swift` · `Sources/ItsClassTime/Services.swift` | 生命周期、单实例、配置保存与登录启动 |
| `Sources/ItsClassTime/OverlayController.swift` · `Sources/ItsClassTime/OverlayViews.swift` | AppKit 浮窗、贴边、三种桌面视图与详情卡 |
| `Sources/ItsClassTime/MenuBarController.swift` · `Sources/ItsClassTime/MenuBarPopoverView.swift` | 菜单栏状态、原生控制面板与菜单 |
| `Sources/ItsClassTime/SettingsView.swift` | 四页设置和双层时段编辑 |
| `Sources/ItsClassTime/BoundaryMonitor.swift` · `Sources/ItsClassTime/NotificationService.swift` | 边界等待、事件去重和系统即时通知 |

## 验收

现有 macOS 记录包括 220 项引擎检查、应用与镜像构建、本机签名校验，以及隔离配置中的启动、单实例和损坏配置恢复冒烟。桌面安装、交互和通知实弹用例按 [验收记录](../docs/ACCEPTANCE.md) 继续复核。

退出应用后，可在隔离数据目录启动调试实例。

```bash
CLASSTIME_DATA_DIR="/tmp/classtime-dev" CLASSTIME_SHOW_SETTINGS=1 \
  ./macos/build/app/ItsClassTime.app/Contents/MacOS/ItsClassTime
```

1. 从镜像拖入应用并启动。关闭设置后，从菜单栏左键打开控制面板，右键打开快捷菜单并恢复设置。
2. 切换长条、圆环和屏幕边框，核对顶部实时预览与桌面同步。测试四边吸附、三击归中、悬停详情、主屏回退和全屏 Spaces。
3. 复制模板与新建空白模板，在时间轴测试拖动、双击编辑及冲突恢复。输入 121 分钟，在周/日课表核对新时段。编辑「仅今天」后核对常规模板。
4. 在「通知」发一条系统测试通知，再用隔离课表等候真实边界。核对同一边界的合并消息、暂停时进度持续、恢复后从下一边界提醒。
5. 核对登录项、重复启动、损坏配置备份与恢复，以及设为只读时的错误提示。桌面验证过程记录系统版本与显示器配置。
