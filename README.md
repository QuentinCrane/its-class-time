<div align="center">

# It's Class Time!

**“上课”时间到！** · 把课表的节奏，放回桌面上。

常驻屏幕边缘的原生小组件 · 分节进度 · 悬停详情 · 到点系统通知 · 完全离线

![平台](https://img.shields.io/badge/platform-Windows%20%7C%20macOS-0b6cbf?style=flat-square)
![技术](https://img.shields.io/badge/native-C%2B%2B%2FWinRT%20%C2%B7%20SwiftUI-444444?style=flat-square)
![状态](https://img.shields.io/badge/Windows-preview-e08b00?style=flat-square)
![依赖](https://img.shields.io/badge/cloud%20%2F%20account-none-2e8b57?style=flat-square)

[它能做什么](#它能做什么) · [为什么做](#为什么做这个) · [装上试试](#装上试试windows) · [现在到哪一步](#现在到哪一步) · [文档地图](#文档地图) · [路线图](#路线图)

</div>

![It's Class Time! 设计总览](assets/prototype/ItsClassTime-设计总览.png)

> 上图是 V1.0 设计总览稿，用于说明产品形态，不是系统截图。真实通知由操作系统绘制，我们不会自己画一个“看起来像通知”的窗口。

## 它能做什么

**It's Class Time!** 把你熟悉的上课铃声搬到桌面上。它不问你今天要完成什么，也不催你打卡，只做一件事：**让你随时知道现在是第几节、这一节走到哪儿了、还有多久能站起来走两步。**

时间线来自一张真实的课表——高中 45 分钟一节，或者大学 90 分钟一大课，中间有课间、有午休。它按本地时钟自己走，到点用系统通知提醒你上下课。

它**不是**番茄钟：没有“开始专注”按钮，没有 25 分钟倒计时，也不需要你每天手动开一轮。

不上学也能用。把模板改成你自己的节奏——上午两段深度工作、午休、下午一段长会——它本质上就是一张可编辑的时间表，只是默认长得像课表。

## 为什么做这个

写代码、写论文、改方案的时候，人对时间的感知会失真。抬头看一眼钟，你只知道“现在三点二十”，但真正想知道的是“这节还剩多久”“下一段休息什么时候来”。

常见做法是设一串闹钟，或者用番茄钟把一天切成等长的小块。可真实的课表不是等长的，午休也不是五分钟。闹钟设多了会麻木，设少了会错过；番茄钟则要求你不停地开始、结束、开始。

所以我们做了个很窄的东西：把课表本身放到屏幕边缘，让它安静地走。你不需要打开它，也不需要维护它——抬头就有答案。

## 你会看到什么

### 一眼读懂时间

- **分节长条**：每一节一根小条。上过的填满，正在上的按比例生长，没到的留白；课间是一小段不同颜色的间隔，午休是明显更长的分隔，不会被误画成一节课。
- **圆环**：一个主环，中心是剩余时间。只想知道“还剩多久”的时候用它。
- **屏幕边框**（Windows）：沿显示器工作区四边画一整圈进度，几乎不占地方，适合满屏工作时使用；颜色跟着当前是上课、课间还是午休走。
- **当前时段 / 全天**：屏幕小就只看这一段；想掌握整天节奏就全铺开。这只是展示过滤，提醒永远基于完整日程。

### 悬停才说话

鼠标停上去，才展开课程名、第几节、起止时间、已过与剩余、百分比、下一段预告。不悬停时保持最窄，不闪、不跳、不抢焦点。四条边都能吸附：上下保持横排，左右自动转成竖向排列。多屏时可在「显示与位置」选择组件所在的显示器，或跟随系统主显示器。默认紧贴任务栏/屏幕边缘，靠边一侧以小圆角接续；想要悬浮感可以关掉「紧贴边缘」。拖歪了？连续点三下它，沿当前边回到居中。

### 改得动的课表

- 高中 / 大学两份模板只是起点，全部可改：改名、改起止、增删时段、调午休，还能按星期启用。
- **数字框精确到 1 分钟，滑块按 5 分钟微调。** 手输 121 分钟不会被滑块悄悄改成 120。
- **Windows 时间轴**：拖动时段或两端调整时间，可选 1、5、15 分钟步进；也可复制时段并在空白处新增。
- **「仅今天」和「永久修改」分开放。** 临时调课不会污染常规模板；改模板也不会偷偷改掉今天。
- 改时长会影响到后面的课时，你可以选「顺延后续」或「后续保持原时间」；冲突就拦下来，并明确指出是哪一行。
- Windows 分节长条可调整整体长度和背景色，也可恢复默认长度与系统背景。

### 靠得住的提醒

- 由**常驻进程在边界到达时**调用系统通知，而不是提前把一整天的事件排进系统队列。
- 睡眠两小时再唤醒，只显示当下的状态，不会补发一串过期提醒。
- 同一下课瞬间的“下课了”和“课间开始”合并成一条，不刷屏。
- 一键暂停提醒：时间照走、进度照动，只是不响；恢复后不补发暂停期间的旧提醒。

### 安静地待着

- 关掉设置窗口不等于退出，托盘 / 菜单栏一直在；从这里可以显示隐藏、切换视图、暂停提醒、退出。
- 单实例运行，不会出现两个进程各发一套通知。
- 换显示器、改缩放后，浮窗会回到仍然可见的工作区域。
- 开机启动是可选项，默认关闭，不会悄悄写启动项。
- 完全离线，配置就是一个本地 JSON。文件损坏时备份原件并回退到预设，而不是打不开。

> 上面写的是设计行为。其中系统通知实弹、睡眠唤醒、托盘恢复、显示器热插拔等场景仍在逐项复核，状态以 [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md) 为准——我们不把“代码写完”当成“验证通过”。

## 两个平台，两套原生

这个项目有一个不太流行的坚持：**不用 Electron、Tauri 或 WebView 做桌面壳。**

|  | Windows 11 | macOS |
|---|---|---|
| 语言与框架 | C++20 · C++/WinRT · WinUI 3 · Win32 | Swift · SwiftUI · AppKit |
| 常驻入口 | 托盘（`Shell_NotifyIcon`） | 菜单栏（`NSStatusItem`） |
| 悬浮窗 | WinUI 3 窗口 + `HWND_TOPMOST` + `WS_EX_NOACTIVATE` | `NSPanel`（浮动层级、不激活） |
| 通知 | `AppNotificationManager.Show` | `UNUserNotificationCenter`（`trigger: nil`） |
| 配置 | 本地 JSON，原子写入 | 本地 JSON，原子写入 |

一个仓库、两个独立工程。共享的只有 JSON Schema、预设和测试向量——不为了“代码复用”塞一个跨语言核心库或 IPC 服务进来。取舍写在 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 和 [docs/DECISIONS.md](docs/DECISIONS.md)。

## 现在到哪一步

这是一个**正在开发中的预览版，不是 1.0**。下面按实际验证情况写，没做到的就不打勾。

| 部分 | 状态 |
|---|---|
| 作息时间引擎、编辑与冲突规则 | Windows 端已通过 162 项检查（共享测试向量、配置兼容性与时间轴拖动），0 失败 |
| Windows 客户端 | 可构建、可安装、可常驻；长条 / 圆环 / 屏幕边框、设置窗口、托盘、悬停详情、拖动吸附已在实机核对 |
| Windows 系统通知 | 代码与文案已完成，授权与真实弹出**待实机复核** |
| Windows 安装包 | 离线 EXE 可用；安装路径、快捷方式、覆盖安装、卸载保留课表已验证；**未签名** |
| macOS 客户端 | 尚未开始，`macos/` 目前是占位目录 |
| CI 与双端 Release | 尚未接入 |

> 时间语义的每一条规则都能在 [docs/FRD.md](docs/FRD.md) 找到出处，验证记录在 [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md)。

### 界面实机截图（Windows 预览版）

![Windows 圆环视图](assets/screenshots/windows-ring.png)

*圆环视图：中心是本节剩余时间，底部是今日已完成的节数。*

![Windows 显示与位置设置](assets/screenshots/windows-display-settings.png)

*显示与位置：三种形态、两种范围、边框粗细，预览随选择即时更新。*

## 装上试试（Windows）

1. 到 **Releases** 下载 `ItsClassTime-Setup-<版本>-x64.exe`。首个 Release 发布之前，请按下面的「从源码构建」自己生成。
2. 双击安装，选择安装路径，可选创建桌面快捷方式。默认装到 `%LOCALAPPDATA%\Programs\ClassTime`，仅当前用户，并创建开始菜单项。
3. 首次启动选择高中或大学模板，再按你的真实课表改。
4. 关掉设置窗口。它会留在托盘里继续计时。

几点需要提前说明：

- 系统要求 Windows 10 1809（x64）及以上，主要面向 Windows 11。
- **安装包没有代码签名**，SmartScreen 可能拦一下；介意的话请从源码构建。
- 在没装 Windows App Runtime 的干净机器上，首次部署流程仍待验证。
- 卸载后个人课表保留在 `%LOCALAPPDATA%\ClassTime`，重装可继续用。

## 从源码构建

**Windows**（C++/WinRT + WinUI 3，不需要 MSBuild 也能构建）：

```cmd
cd windows
tools\build-unpackaged.cmd
:: 产物：build\app\ClassTimeApp.exe
```

打离线安装包（需要 Inno Setup 7）：

```powershell
.\windows\tools\build-installer.ps1
```

完整的环境要求、依赖版本、已知限制和手工验收清单见 [windows/README.md](windows/README.md)。

**macOS**：工程尚未创建。计划用 Xcode 新建 SwiftUI App，先用 AppKit 验证菜单栏常驻、不抢焦点的浮动面板和系统即时通知，再接同一套时间语义测试。步骤见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

两端都必须读取 `shared/presets/` 的预设，并跑通 `shared/test-cases/` 的同一组测试向量——时间规则不允许“各写各的”。

仓库结构：

```text
It'sClassTime/
├── windows/     # C++/WinRT 工程、作息引擎、测试、构建与打包脚本
├── macos/       # 占位，待创建
├── shared/      # 双端共用的 JSON Schema、预设、测试向量
├── docs/        # 产品、功能、设计、架构、验收文档
├── assets/      # 原型图与实机截图
└── AGENTS.md    # 给 AI 编码助手的硬约束
```

> 工程标识（`ClassTime.sln`、`ClassTimeApp.exe`、配置目录 `ClassTime`）沿用历史命名，界面与文档中的产品名统一为 **It's Class Time!**（中文 **“上课”时间到！**）。

## 文档地图

| 文档 | 回答什么问题 |
|---|---|
| [docs/PRD.md](docs/PRD.md) | 为谁做、先做什么、明确不做什么 |
| [docs/FRD.md](docs/FRD.md) | 时间怎么算、边界怎么判、编辑与通知的确定性规则（可直接照着写代码） |
| [docs/DESIGN.md](docs/DESIGN.md) | 长什么样、悬浮层怎么交互、无障碍与系统视觉边界 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 两端各用哪些原生 API、为什么不做跨平台核心 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 本地怎么建工程、怎么发版、哪些步骤不能省 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 现在做到哪、下一个检查点是什么 |
| [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md) | 怎么手工验收、双端一致性凭什么算通过 |
| [docs/DECISIONS.md](docs/DECISIONS.md) | 哪些已经冻结、哪些要等实测再定 |
| [docs/REFERENCES.md](docs/REFERENCES.md) | 用到的 Apple / Microsoft / GitHub 官方文档 |
| [AGENTS.md](AGENTS.md) | 给 AI 编码助手（也给人）的硬约束 |
| [windows/README.md](windows/README.md) | Windows 端的构建、验证边界与实现映射 |
| [shared/schemas/schedule.schema.json](shared/schemas/schedule.schema.json) | 作息 JSON 的结构契约 |
| [shared/presets/](shared/presets) | 高中 / 大学可编辑初始模板 |
| [shared/test-cases/time-engine.json](shared/test-cases/time-engine.json) | 双端共用的时间引擎测试向量 |

## 我们坚持不做的

产品边界也是产品的一部分。以下这些不是“以后再说”，而是明确不做：

- 不做账号、云同步、团队协作、数据看板。你的课表只在本机。
- 不做 AI 建议、待办管理、日历同步、打卡统计。
- 不提前把整天的通知排给系统，也不自己绘制伪系统通知。
- 不用 WebView / 浏览器内核当 UI，不为跨平台牺牲原生体验。
- 不用 1 秒一轮的后台忙轮询驱动作息，也不拿 UI 帧循环当计时器。
- 不承诺“零内存占用”。资源开销要测出来再说，测量记录写进验收清单。

## 路线图

| 阶段 | 目标 | 状态 |
|---|---|---|
| M0 | 原生技术可行性：常驻、浮窗、即时通知 | Windows 主体通过，通知待复核；macOS 未开始 |
| M1 | 作息引擎：预设、校验、进度、下一边界 | Windows 完成并通过共享向量；macOS 待实现 |
| M2 | 可日常使用的 MVP：两种视图、编辑、暂停、开机启动 | Windows 大部分完成，细节补验中 |
| M3 | 稳定性与发布：多显示器、单实例、CI、双端 Release | 安装包已可用，其余进行中 |

后续功能池（自定义铃声、更多主题、跨设备同步）不在首版范围内，需要单独立项。详见 [docs/ROADMAP.md](docs/ROADMAP.md)。

## 参与

- 如果这个东西正好解决了你的问题，点个 Star 就是最直接的鼓励；用着不顺手，欢迎开 Issue 说说卡在哪。
- 发现问题是好事：请开 Issue，并写清系统版本、显示缩放、课表形态和复现步骤。截图比描述有用。
- 提 PR 前请先读 [AGENTS.md](AGENTS.md)。改时间规则时必须同时更新 `shared/test-cases/time-engine.json`，否则两端会悄悄分叉。
- 建议标签：`windows`、`macos`、`shared-spec`、`bug`、`design`、`release`。一个需求可以拆成两个平台的子任务。

## 许可证

本仓库目前**尚未附带开源许可证**，在正式对外分发前会确定。在此之前，请不要将代码用于商业分发；引用需求文档与设计图时请注明出处。
