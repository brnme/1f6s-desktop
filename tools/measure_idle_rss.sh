#!/usr/bin/env bash
# 空载内存实测:offscreen 启动应用 → 稳定 5s → 采 3 次 /proc/<pid>/status
# 的 VmRSS(间隔 1s)取中位数 → kill。交付 M4「空载内存」数据,不进 ctest。
#
# 用法: tools/measure_idle_rss.sh [二进制路径]
#   默认二进制 build/1f6s-desktop;需已构建。
#   QT_QPA_PLATFORM=offscreen 无头运行;冒烟钩子不启用(手动 kill 收尾)。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$ROOT/build/1f6s-desktop}"
STABLE_SEC="${STABLE_SEC:-5}"

if [ ! -x "$BIN" ]; then
    echo "错误:找不到可执行文件 $BIN(先 cmake --build build)" >&2
    exit 1
fi

# 独立 XDG_CONFIG_HOME,不碰开发机自己的 QSettings 配置。
TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"; kill "$PID" 2>/dev/null || true' EXIT

QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$TMPD" "$BIN" &
PID=$!

sleep "$STABLE_SEC"
if ! kill -0 "$PID" 2>/dev/null; then
    echo "错误:应用在采样前退出(疑似启动崩溃)" >&2
    exit 1
fi

samples=()
for _ in 1 2 3; do
    kb="$(awk '/^VmRSS:/{print $2}' "/proc/$PID/status" 2>/dev/null || true)"
    [ -n "$kb" ] || { echo "错误:读取 /proc/$PID/status 失败" >&2; exit 1; }
    samples+=("$kb")
    sleep 1
done

median="$(printf '%s\n' "${samples[@]}" | sort -n | sed -n 2p)"

echo "VmRSS 三次采样 (KB): ${samples[*]}"
echo "空载 RSS 中位数: ${median} KB ≈ $(( median / 1024 )) MB"
# 判定基线:M4 目标 < 80 MB
if [ "$median" -lt 81920 ]; then
    echo "结论:达标(< 80 MB)"
else
    echo "结论:超目标(≥ 80 MB),需排查"
    exit 2
fi
