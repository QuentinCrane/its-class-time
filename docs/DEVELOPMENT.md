# DEVELOPMENT｜本地开发与 GitHub 维护指南

## 1. 本地开发环境

| 平台 | 建议开发机 | 必备软件 | 验证目标 |
|---|---|---|---|
| macOS | Apple Silicon Mac | Xcode / macOS SDK / Swift | SwiftUI 配置窗口 + AppKit 悬浮条、菜单栏和通知 |
| Windows 11 | x64 Windows 电脑 | Visual Studio 2026 + C++ 桌面工具 + Windows SDK + Windows App SDK / WinUI C++ 模板 | C++/WinRT WinUI3 设置窗口 + Win32 悬浮/托盘 + 通知 |

**具体 SDK 版本安装前核对官方链接**，不要把曾经某一日的最新版写死在生成工程里（见 `REFERENCES.md`）。先用当前官方模板生成真实工程并验证编译。

## 2. 建仓方式（Monorepo）

```text
ClassTime/
├── macos/                   # Xcode 真实工程（待生成）
├── windows/                 # VS C++/WinRT 真实工程（待生成）
├── shared/
│   ├── schemas/
│   ├── presets/
│   └── test-cases/
├── assets/prototype/
├── docs/
├── .github/workflows/       # 先完成手动构建，再加入真实 CI
├── AGENTS.md
└── README.md
```

**不要创建 `macos` 和 `windows` 两条长期分支**。只使用一个 `main` 和短期功能分支；PR 统一评审。单项需求可以用一个 GitHub Issue 管理两个平台的子任务。

建议标签：`macos`、`windows`、`shared-spec`、`bug`、`design`、`release`。每个里程碑统一版本号（例如 `v0.1.0`、`v0.2.0`）。

## 3. 分阶段启动步骤

### Step 0：创建项目

1. Windows：新建**C++ WinUI 3 Desktop** 项目（不要误选 C#），先编译空窗口。
2. macOS：新建 SwiftUI macOS App，先编译空窗口。
3. 将本开发包中的 docs、shared、assets、AGENTS、README 放进仓库根目录；不要把 `.xcodeproj`、`.slnx` 的路径写进文档然后假装项目已存在。
4. 用各自 IDE 将平台工程目录加入 Git。

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

- GitHub Actions 用 `macos-*` runner 构建 Xcode 工程，`windows-*` runner 构建 Visual Studio C++ 工程。
- 先构建、测试、上传 CI artifacts；无签名时**不标成正式可安装发行包**。
- 通过本地安装测试后，再配置 Apple Developer ID + notarization，Windows MSIX 签名/信任链，并统一到单个 GitHub Release。
- 版本号与 changelog 统一维护；mac 和 windows 可各自标注“本次改动”。

## 4. Git 使用建议

```bash
# 本地初始化（在复制好文档的目录中执行）
git init
git add README.md AGENTS.md docs shared assets
git commit -m "docs: freeze ClassTime native desktop v1 requirements"
```

随后分别用 Xcode 和 Visual Studio 创建工程后继续提交。无需 monorepo 管理框架、Git submodule、Git LFS（当前原型图片很小）、NPM/Node 构建链。

## 5. 发行重要说明

- GitHub Actions 可自动化**构建**，但 macOS 公证与 Windows MSIX 签名是另外的步骤，需要开发者身份、证书/权限等前提。
- 试验版本可先作为本地调试程序运行。别在还没解决签名时写“零提示安装”一类不真实承诺。
- 当前资料包**未声称完成双端构建**；有架构、配置示例和测试数据，无 `.app`、`.exe` 成品。
