#!/usr/bin/env bash
# 构建 macOS 离线分发镜像：macos/build/release/ItsClassTime-<version>-<arch>.dmg
# 与日常开发共用 macos/build/app/ItsClassTime.app 产物（对标 windows/tools/build-installer.ps1）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION="0.1.1"
ARCH="$(uname -m)"
APP_BUNDLE="${MACOS_DIR}/build/app/ItsClassTime.app"
STAGING_DIR="${MACOS_DIR}/build/dmg-staging"
RELEASE_DIR="${MACOS_DIR}/build/release"
DMG_PATH="${RELEASE_DIR}/ItsClassTime-${VERSION}-${ARCH}.dmg"

"${SCRIPT_DIR}/build-app.sh"

echo "==> 组装离线 DMG 安装镜像..."
rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}" "${RELEASE_DIR}"

cp -R "${APP_BUNDLE}" "${STAGING_DIR}/ItsClassTime.app"
ln -s /Applications "${STAGING_DIR}/Applications"

cat > "${STAGING_DIR}/安装说明.txt" <<'EOF'
It's Class Time!（“上课”时间到！）· macOS 原生预览版
================================================

1. 将 ItsClassTime.app 拖入右侧 Applications（应用程序）文件夹即可完成安装。
2. 首次打开会显示设置窗口：可选择高中或大学初始模板，并按你的真实课表修改。
3. 关闭设置窗口不会退出程序，菜单栏图标与屏幕边缘悬浮进度条会继续安静运行。
4. 个人课表与设置仅保存在本机：
   ~/Library/Application Support/ClassTime/config.json
   无需账号、完全离线；覆盖升级或删除应用均不会清除你的课表。

注意：当前预览构建使用本地 ad-hoc 签名，尚未附加 Apple Developer ID 签名与公证（Notarization）。
若从网络下载后首次打开被系统拦截，可在「系统设置 → 隐私与安全性」中点击“仍要打开”。
EOF

rm -f "${DMG_PATH}"
hdiutil create \
  -volname "It's Class Time!" \
  -srcfolder "${STAGING_DIR}" \
  -ov \
  -format UDZO \
  "${DMG_PATH}" >/dev/null

rm -rf "${STAGING_DIR}"

echo ""
echo "离线安装镜像已生成：${DMG_PATH}"
