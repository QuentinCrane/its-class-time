# ClassTime｜纯原生双平台桌面作息时间感知应用

> **项目名称：ClassTime（工作名，可改）**｜需求基线：v1.0｜2026-11-21  
> 状态：需求已确认；提供的是**开发文档与参考原型，不是已经实现的可运行应用**。

## 1. 产品一句话

把高中/大学的上课、下课、课间与午休节奏搬到电脑桌面，用常驻屏幕边缘的分节进度条或圆环，让学习/办公时可直观看到时间推进，并由**运行中的原生应用主动调用系统通知**提示作息切换。

**不是**待办软件、日历应用、账号服务，也不以固定 25+5 番茄钟为核心。

## 2. 已冻结的关键决策

- **严格纯原生双端**：macOS `Swift + SwiftUI + AppKit`；Windows 11 `C++20 + C++/WinRT + WinUI 3 + Win32 + Windows App SDK`；不用 Electron、Tauri、WebView 或 C#/.NET。
- **一个 GitHub 仓库、两个独立原生工程**；只共享数据规范、预设和测试向量，避免跨语言桥接核心库。
- **作息**：高中、大学两份**可编辑**的默认预设；本地真实时钟驱动，不用每节手动开始。
- **时间调整**：今日临时覆盖 / 永久修改模板；编辑提供精确**数字输入**与方便调整的**滑动条**，午休和长休息同样支持。
- **悬浮**：默认分节长条，另有圆环；可切换 **当前时段 / 全天** 显示范围；边缘吸附、置顶、hover 详情、低干扰。
- **通知**：程序常驻后台，作息边界到达时主动发**即时系统通知**；并非提前向系统登记一天的计划通知。
- **暂停**：一键暂停**提醒**，不暂停系统时间或课程进度；设备睡眠唤醒后不补发成串的旧通知。
- **轻量**：离线、本地配置、不引入服务端/数据库/账户/AI/统计大屏/繁琐框架。

## 3. 目录导航

| 文件 | 用途 |
|---|---|
| [docs/PRD.md](docs/PRD.md) | 产品目标、用户路径、功能优先级、非目标 |
| [docs/FRD.md](docs/FRD.md) | 可直接编码的状态、时段、通知、编辑与验收规则 |
| [docs/DESIGN.md](docs/DESIGN.md) | 原型说明、双端视觉、浮窗交互、无障碍 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Swift/AppKit 与 C++/WinRT/Win32 的最小架构 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 从本地建工程到 GitHub CI/发布的步骤 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 开发优先级、阶段检查点 |
| [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md) | 手工验收与双端一致性清单 |
| [docs/DECISIONS.md](docs/DECISIONS.md) | 已决定与尚需在实现中验证的边界 |
| [docs/REFERENCES.md](docs/REFERENCES.md) | Apple/Microsoft/GitHub 官方文档链接 |
| [AGENTS.md](AGENTS.md) | 供 Codex/AI 编码助手遵守的约束 |
| [assets/prototype/ClassTime-Concept-v1.png](assets/prototype/ClassTime-Concept-v1.png) | 完整产品原型参考图 |
| [shared/schemas/schedule.schema.json](shared/schemas/schedule.schema.json) | 作息 JSON Schema（示例层） |
| `shared/presets/*.json` | 高中、大学可编辑初始样例 |
| `shared/test-cases/time-engine.json` | 平台通用时间引擎测试向量 |

![已确认的概念原型](assets/prototype/ClassTime-Concept-v1.png)

> **图为视觉概念**，并非操作系统截图；图中时间、日期、字体、菜单、系统通知样式和 UI 排布均非逐像素实现承诺。系统通知必须调用 OS 官方接口，不能自己仿制通知窗口。

## 4. 本地启动开发（推荐）

1. **Windows**：在 Visual Studio（含 C++ 桌面开发、WinUI/Windows App SDK 工具）新建 **C++/WinRT WinUI 3 Desktop Packaged** 项目到 `windows/`，先做系统托盘、悬浮 HWND、即时通知三个技术验证。
2. **macOS**：在 Xcode 新建 SwiftUI macOS App 到 `macos/`，用 AppKit 管理菜单栏/悬浮面板，先做原生通知、常驻和悬浮窗口技术验证。
3. 两端各自读取 `shared/presets/` 示例，并运行 `shared/test-cases/` 同样的时间语义测试。
4. 原型验收通过后，再开展两种视图、今日调整、模板编辑和发布包装。

**仓库暂时不提供假装可运行的 `.slnx`、`.xcodeproj` 或 CI 配置。** 先由 IDE 生成真实项目；通过技术验证之后，再写可实际执行的两条 Actions 工作流。具体顺序见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。
