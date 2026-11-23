# DEVELOPMENT｜本地开发与 GitHub 维护指南

> **这份文档回答**：拿到仓库后怎么在本机跑起来、按什么顺序推进、发布前哪些步骤不能省。Windows 端已经有可运行的工程，macOS 端仍待创建；下面的步骤按“当前实际状态”标注。

## 1. 本地开发环境

| 平台 | 建议开发机 | 必备软件 | 验证目标 |
|---|---|---|---|
| macOS | Apple Silicon Mac | Xcode / macOS SDK / Swift | SwiftUI 配置窗口 + AppKit 悬浮条、菜单栏和通知 |
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
# 需要 Inno Setup 7，产物 build\release\ItsClassTime-Setup-<版本>-x64.exe
.\windows\tools\build-installer.ps1
```

引擎可以脱离 UI 单独验证：

```powershell
cmake -S windows -B windows/build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build/engine
windows\build\engine\engine_tests.exe "<仓库绝对路径>"
```

详细依赖版本、实机验证边界与已知限制见 [windows/README.md](../windows/README.md)。

## 2. 建仓方式（Monorepo）

```text
It'sClassTime/
├── macos/                   # Xcode 真实工程（占位，待生成）
├── windows/                 # VS C++/WinRT 真实工程（已生成，可构建运行）
├── shared/
│   ├── schemas/
│   ├── presets/
│   └── test-cases/
├── assets/
│   ├── prototype/
│   └── screenshots/
├── docs/
├── .github/workflows/       # 尚未添加工作流，先保证本机可构建
├── AGENTS.md
└── README.md
```

**不要创建 `macos` 和 `windows` 两条长期分支**。只使用一个 `main` 和短期功能分支；PR 统一评审。单项需求可以用一个 GitHub Issue 管理两个平台的子任务。

建议标签：`macos`、`windows`、`shared-spec`、`bug`、`design`、`release`。每个里程碑统一版本号（例如 `v0.1.0`、`v0.2.0`）。

## 3. 分阶段启动步骤

### Step 0：创建项目

1. Windows：**已有** `windows/ClassTime.sln`（C++ WinUI 3 Desktop）。若要重建，注意选 C++ 而不是 C#，并保留现有的纯代码 UI 装配方式。
2. macOS：新建 SwiftUI macOS App，先编译空窗口。
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

- **当前尚未接入 CI。** 先在两端完成本机构建与安装验证，再写“确实能跑通”的工作流，而不是先提交一份没人跑过的 YAML。
- 计划：`macos-*` runner 构建 Xcode 工程，`windows-*` runner 构建 C++ 工程并跑引擎测试；先上传 artifacts。
- 无签名的产物**不标成正式可安装发行包**。Windows 安装包目前就是未签名状态，需明确告知 SmartScreen 风险。
- 通过本地安装测试后，再配置 Apple Developer ID + notarization、Windows 代码签名/信任链，并统一到单个 GitHub Release。
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
- 提交信息建议带范围前缀：`feat(windows):`、`fix(engine):`、`docs:`、`chore:`。
- 不需要 monorepo 管理框架、Git submodule、Git LFS 或 NPM/Node 构建链。
- Windows 构建产物（`windows/build`、`windows/packages`）已在 `.gitignore` 中，不要提交编译器生成的中间文件。

## 5. 发行重要说明

- GitHub Actions 可自动化**构建**，但 macOS 公证与 Windows 代码签名是另外的步骤，需要开发者身份、证书/权限等前提。
- 试验版本可先作为本地调试程序运行。别在还没解决签名时写“零提示安装”一类不真实承诺。
- 当前状态：**Windows 有可运行源码与未签名安装包**；macOS 尚无工程，也没有 `.app` 成品。双端 Release 尚未发布。
- Windows 的通知注册与开机启动属于打包相关能力，解包模式下只能验证界面与引擎；相关结论要注明是在哪种形态下测得的。
