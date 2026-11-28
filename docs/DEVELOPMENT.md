# DEVELOPMENT｜本地开发与发行

仓库包含独立的 Windows 和 macOS 原生工程。双端共用 JSON Schema、预设和时间引擎测试向量。

## 本地构建

| 平台 | 开发环境 | 构建指南 |
| --- | --- | --- |
| Windows x64 | Visual Studio 2022 17.8+ 或 Build Tools、Windows SDK 10.0.22621+、NuGet。打包使用 Inno Setup 7。 | [Windows](../windows/README.md) |
| macOS | 提供 macOS 27 SDK 的 Command Line Tools 或 Xcode。正式发行在 Apple Silicon 上构建。 | [macOS](../macos/README.md) |

Windows 在仓库根目录还原 NuGet 依赖并生成投影头文件，再构建程序或离线安装包。 [Windows 构建工作流](../.github/workflows/build-windows.yml)

```powershell
.\windows\tools\build-unpackaged.cmd
.\windows\tools\build-installer.ps1
```

macOS 在仓库根目录运行。两个脚本均从共享预设组装应用。

```bash
./macos/tools/build-app.sh
./macos/tools/build-dmg.sh
```

| 产物 | 路径 |
| --- | --- |
| Windows 解包程序 | `windows/build/app/ClassTimeApp.exe` |
| Windows 安装包 | `windows/build/release/ItsClassTime-<版本>-Windows-x64-Setup.exe` |
| macOS 应用包 | `macos/build/app/ItsClassTime.app` |
| macOS 镜像 | `macos/build/release/ItsClassTime-<版本>-macOS-<架构>.dmg` |

## 验证改动

时间规则以功能规格为准。修改时间语义时同步更新两端实现与共享测试向量。 [功能规格](FRD.md) · [测试向量](../shared/test-cases/time-engine.json)

Windows 在 x64 MSVC 开发终端运行。

```powershell
cmake -S windows -B windows/build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build/engine
.\windows\build\engine\engine_tests.exe (Get-Location).Path
```

macOS 可以单独运行 Swift 引擎检查。

```bash
swift run --package-path macos engine-tests "$(pwd)"
```

桌面验收使用 `CLASSTIME_DATA_DIR` 指向隔离作息数据。测试通知权限、登录启动和升级保留课表时，记录使用的安装形态及构建提交。资源采样同时记录系统版本、显示器配置、时长与组件状态。 [验收记录](ACCEPTANCE.md) · [路线图](ROADMAP.md)

## 维护仓库

- 从 `main` 建立短期分支，Windows 和 macOS 改动均按主题提交。提交标题可用 `feat(windows):`、`feat(macos):` 或 `docs:` 等范围前缀。
- `assets/branding` 保存 README 封面，`assets/screenshots` 保存实机界面，`assets/prototype` 保存设计参考。
- 编译产物和本地依赖分别放入 `windows/build`、`windows/packages`、`macos/.build` 或 `macos/build`。
- `docs/` 管理产品规格、设计、架构、验收与发行说明。 [协作说明](../AGENTS.md)

## 发行

Windows 构建工作流可单独执行。双端发行工作流在引擎检查与两端打包通过后创建 GitHub Release，上传 Windows x64 EXE、macOS arm64 DMG 和 `SHA256SUMS.txt`。 [双端发行工作流](../.github/workflows/release.yml)

1. 同步 Windows 包清单、应用清单、资源版本和设置版本。同步 macOS `Info.plist` 与设置版本。
2. 添加 `docs/releases/v<版本>.md`，按平台记录变更、系统要求和安装签名状态。
3. 以 `release: v<版本>` 为提交标题推送到 `main`，并包含工作流监听的路径。也可在 Actions 手动运行对应工作流，传入相同的版本标签。每个发行使用新的标签。
4. 核对公开下载页的两个文件和 SHA-256，按平台运行安装与桌面验收。

当前 Windows 离线 EXE 未签名。macOS 应用使用 Ad-hoc 签名，Developer ID 签名与 Apple 公证待完成。 [v0.1.2 发行说明](releases/v0.1.2.md)
