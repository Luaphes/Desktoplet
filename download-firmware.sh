#!/usr/bin/env bash
# 从 GitHub Release 下载固件 .bin（通过 ghproxy.net 加速）
# 用法: ./download-firmware.sh <version>
# 示例: ./download-firmware.sh 6

set -e
VERSION="${1:?Usage: $0 <release-version>}"
GH_REPO="Luaphes/Desktoplet"
DEST="/root/esp32-firmware/firmware.bin"

echo "[*] Downloading v${VERSION} firmware.bin from ${GH_REPO}..."
curl -L "https://ghproxy.net/https://github.com/${GH_REPO}/releases/download/v${VERSION}/firmware.bin" \
  -o "${DEST}"
echo "[✓] Saved to ${DEST} ($(du -h ${DEST} | cut -f1))"
