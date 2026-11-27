#!/usr/bin/env bash
# 构建 macOS 原生应用包：macos/build/app/ItsClassTime.app
# 无需完整 Xcode.app，仅依赖 macOS Command Line Tools（swift / iconutil / codesign / plutil）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${MACOS_DIR}/.." && pwd)"

APP_NAME="ItsClassTime"
BUNDLE_DIR="${MACOS_DIR}/build/app/${APP_NAME}.app"
CONTENTS_DIR="${BUNDLE_DIR}/Contents"
MACOS_BIN_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
ICONSET_DIR="${MACOS_DIR}/build/icon/AppIcon.iconset"

if pgrep -x "${APP_NAME}" >/dev/null 2>&1; then
  echo "提示：检测到正在运行的 ${APP_NAME} 进程，请先从菜单栏点击「退出 It's Class Time!」后再重新打包。" >&2
  exit 1
fi

# macOS 自带 Bash 3.2 在 set -u 下展开空数组会报错，用函数传递可选参数。
swiftpm() {
  local subcommand="$1"
  shift
  if [[ "${CLASSTIME_SWIFTPM_DISABLE_SANDBOX:-0}" == "1" ]]; then
    swift "${subcommand}" --disable-sandbox "$@"
  else
    swift "${subcommand}" "$@"
  fi
}

echo "==> [1/4] 运行共享作息时间引擎对等测试..."
swiftpm run --package-path "${MACOS_DIR}" engine-tests "${REPO_ROOT}"

echo "==> [2/4] 编译 Release 原生二进制..."
swiftpm build -c release --package-path "${MACOS_DIR}" --product "${APP_NAME}"
BIN_PATH="$(swiftpm build -c release --package-path "${MACOS_DIR}" --show-bin-path)/${APP_NAME}"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "错误：未找到编译产物 ${BIN_PATH}" >&2
  exit 1
fi

echo "==> [3/4] 生成 Retina 应用图标并组装 ${APP_NAME}.app..."
rm -rf "${ICONSET_DIR}" "${BUNDLE_DIR}"
mkdir -p "${ICONSET_DIR}" "${MACOS_BIN_DIR}" "${RESOURCES_DIR}/presets"

swift "${SCRIPT_DIR}/generate-icon.swift" "${ICONSET_DIR}"
iconutil -c icns "${ICONSET_DIR}" -o "${RESOURCES_DIR}/AppIcon.icns"

cp "${BIN_PATH}" "${MACOS_BIN_DIR}/${APP_NAME}"
chmod +x "${MACOS_BIN_DIR}/${APP_NAME}"

# 修复 SwiftPM 产物将 LC_BUILD_VERSION 的 sdk 错标为 minos(13.0) 导致 macOS 26/27 回退旧外观的问题：
# 保持最低部署系统 13.0 不变，将 linked SDK 版本校准为当前构建机 macOS SDK 版本以启用原生 Liquid Glass。
SDK_VERSION="$(xcrun --sdk macosx --show-sdk-version)"
if command -v vtool >/dev/null 2>&1 && [[ -n "${SDK_VERSION}" ]]; then
  echo "    校准 Mach-O LC_BUILD_VERSION: minos=13.0, sdk=${SDK_VERSION}"
  vtool -set-build-version macos 13.0 "${SDK_VERSION}" \
    "${MACOS_BIN_DIR}/${APP_NAME}" -output "${MACOS_BIN_DIR}/${APP_NAME}"
fi

cp "${MACOS_DIR}/Resources/Info.plist" "${CONTENTS_DIR}/Info.plist"
plutil -lint "${CONTENTS_DIR}/Info.plist" >/dev/null

cp "${REPO_ROOT}/shared/presets/high-school.json" "${RESOURCES_DIR}/presets/high-school.json"
cp "${REPO_ROOT}/shared/presets/university.json" "${RESOURCES_DIR}/presets/university.json"

echo "==> [4/4] 执行本机 Ad-hoc 签名与完整性校验..."
codesign --force --deep --sign - "${BUNDLE_DIR}"
codesign --verify --deep --strict "${BUNDLE_DIR}"

echo ""
echo "构建完成：${BUNDLE_DIR}"
