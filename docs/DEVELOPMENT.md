# DEVELOPMENT｜本地开发与 GitHub 维护指南

> **这份文档回答**：拿到仓库后怎么在本机跑起来、按什么顺序推进、发布前哪些步骤不能省。Windows 与 macOS 两端目前均已有可本机直接构建、跑通测试并打包的原生工程（见 [windows/README.md](../windows/README.md) 与 [macos/README.md](../macos/README.md)）。

## 1. 本地开发环境

| 平台 | 建议开发机 | 必备软件 | 验证目标 |
|---|---|---|---|
| macOS | Apple Silicon Mac | Xcode 27 或提供 macOS 27 SDK 的 Command Line Tools | SwiftUI 配置窗口 + AppKit 悬浮条、菜单栏和通知 |
| Windows 11 | x64 Windows 电脑 | Visual Studio 2022 17.8+（含“使用 C++ 的桌面开发”与 C++ WinUI 工具）、Windows SDK 10.0.22621+、Windows App SDK | C++/WinRT WinUI3 设置窗口 + Win32 悬浮/托盘 + 通知 |

**具体 SDK 版本安装前核对官方链接**，不要把曾经某一日的最新版写死在生成工程里（见 `REFERENCES.md`）。先用当前官方模板生成真实工程并验证编译。

### Windows 最短上手路径

```cmd
cd windows
:: 直接 cl/link，产物 build\app\ClassTimeApp.exe
tools\build-unpackaged.cmd
```

不依赖 MSBuild，也不依赖 XAML 编译器；适用于被组策略限制的开发机。需要验证安装包时：

```powershell
# 需要 Inno Setup 7，产物 build\release\ItsClassTime-<版本>-Windows-x64-Setup.exe
.\windows\tools\build-installer.ps1
```

引擎可以脱离 UI 单独验证：

```powershell
cmake -S windows -B windows/build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build/engine
windows\build\engine\engine_tests.exe "<仓库绝对路径>"
```

详细依赖版本、实机验证边界与已知限制见 [windows/README.md](../windows/README.md)。

### macOS 最短上手路径

```bash
# 需要 macOS 27 SDK；可使用 Command Line Tools，无需完整 Xcode.app
./macos/tools/build-app.sh
# 产物：macos/build/app/ItsClassTime.app

# 打包离线安装镜像（产物：macos/build/release/ItsClassTime-<版本>-macOS-arm64.dmg）
./macos/tools/build-dmg.sh

# 单独验证作息时间引擎（220 项共享与对等检查）
swift run --package-path macos engine-tests "$(pwd)"
```

详细实现映射与手工验收步骤见 [macos/README.md](../macos/README.md)。

## 2. 建仓方式（Monorepo）

```text
It'sClassTime/
├── macos/                   # SwiftUI + AppKit 原生工程（可构建 .app / .dmg 并跑通 220 项引擎检查）
├── windows/                 # C++/WinRT + WinUI 3 原生工程（可构建 .exe 与离线安装包并跑通 244 项引擎检查）
├── shared/
│   ├── schemas/
│   ├── presets/
│   └── test-cases/
├── assets/
│   ├── prototype/
│   └── screenshots/
├── docs/
├── .github/workflows/       # Windows 构建与双端发行工作流
├── AGENTS.md
└── README.md
```

**不要创建 `macos` 和 `windows` 两条长期分支**。只使用一个 `main` 和短期功能分支；PR 统一评审。单项需求可以用一个 GitHub Issue 管理两个平台的子任务。

建议标签：`macos`、`windows`、`shared-spec`、`bug`、`design`、`release`。每个里程碑统一版本号（例如 `v0.1.0`、`v0.2.0`）。

## 3. 分阶段启动步骤

### Step 0：创建项目

1. Windows：**已有** `windows/ClassTime.sln`（C++ WinUI 3 Desktop）与无 MSBuild 直构脚本 `tools/build-unpackaged.cmd`。
2. macOS：**已有** `macos/Package.swift`（SwiftUI + AppKit）与 `tools/build-app.sh` / `tools/build-dmg.sh`，仅需 Command Line Tools 即可构建签名。
3. 仓库内的 docs、shared、assets、AGENTS、README 是跨端契约；新建工程时不要复制出第二份。
4. 用各自 IDE 将平台工程目录加入 Git，构建产物由 `.gitignore` 排除。

### Step 1：验证最危险的原生能力

- macOS：关掉设置窗口以后菜单栏图标仍在；有浮动进度面板；在 app 运行时调用系统通知；点击托盘/菜单可退出。
- Windows：关闭设置窗口后 `exe` 保持常驻；托盘能恢复浮窗；`HWND_TOPMOST` 不抢焦点；`AppNotificationManager.Show` 发送即时通知。
- 验证多显示器和系统全屏限制；记录观察结果，不用复杂代码规避所有系统行为。

### Step 2：时间引擎和 JSON

- 让两个原生工程均能读取 `shared/presets` 的格式，转为相同内部类型。
- 按 `FRD.md` 写出段选择、进度、下个边界、日期覆盖、编辑冲突判断。
- 使用 `shared/test-cases/time-engine.json` 检查一致性，不要依赖截图验证时间计算。

### Step 3：配置和交互

- 作息编辑（时间、数字分钟框、滑动条、今日/永久、顺延/保持）。
- 长条和圆环、当前时段/全天、hover、边缘吸附、系统主题。
- 设置菜单、隐藏/恢复、暂停提醒、开机登录选项。

### Step 4：稳定性和资源观测

- 两个平台分别连续运行；记录 CPU、工作集/常驻内存、唤醒次数、用户感知的绘制开销。
- 测试睡眠唤醒、时钟调整、跨日、权限拒绝、explorer/桌面环境重启、外接屏幕拔插。
- 先定位具体瓶颈再优化，不要未经测量重写引擎或为了省极少的运行时间引入不维护的底层依赖。

### Step 5：CI / Release

- Windows 和 macOS 构建工作流分别运行引擎检查并生成安装包；发行工作流仅在两端构建成功后创建 GitHub Release。
- 发行时同步更新双端版本元数据，新增 `docs/releases/v<版本>.md`，以 `release: v<版本>` 为提交标题推送到 `main`；也可手动运行发行工作流。工作流拒绝覆盖已有标签或 Release。
- 当前 Windows 安装包未签名、macOS 应用仅 Ad-hoc 签名且未公证，发行说明必须明确提示 SmartScreen 和 Gatekeeper 的限制。
- 继续补充双端实机安装验证，并配置 Apple Developer ID、公证与 Windows 代码签名。
- 版本号与 changelog 统一维护；mac 和 windows 可各自标注“本次改动”。

## 4. Git 使用建议

仓库已经初始化，日常按短分支推进即可：

```bash
git switch -c feat/windows-hover-fix
git add windows docs
git commit -m "feat(windows): refine hover card placement"
git push -u origin feat/windows-hover-fix
```

- 一个分支只做一件事，合并前保持 `main` 可构建。
- 提交信息建议带范围前缀：`feat(windows):`、`feat(macos):`、`fix(engine):`、`docs:`、`chore:`。
- 不需要 monorepo 管理框架、Git submodule、Git LFS 或 NPM/Node 构建链。
- 两端构建产物（`windows/build`、`windows/packages`、`macos/.build`、`macos/build`）均已在 `.gitignore` 中，不要提交编译器生成的中间文件。

## 5. 发行重要说明

- GitHub Actions 可自动化**构建**，但 macOS 公证与 Windows 代码签名是另外的步骤，需要开发者身份、证书/权限等前提。
- 试验版本可先作为本地调试程序运行。别在还没解决签名时写“零提示安装”一类不真实承诺。
- 当前状态：**Windows 提供未签名离线安装 EXE；macOS 提供 Ad-hoc 签名的 `ItsClassTime.app` 与离线 `.dmg` 镜像**。双端正式签名与公证尚未完成。
- Windows 的通知注册与开机启动属于打包相关能力，解包模式下只能验证界面与引擎；相关结论要注明是在哪种形态下测得的。
