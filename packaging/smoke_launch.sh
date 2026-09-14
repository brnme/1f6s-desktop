#!/usr/bin/env bash
# smoke_launch.sh — 打包产物 offscreen 冒烟:启动 exe,等 1F6S_SMOKE_EXIT_MS
# 毫秒后自退;超时未退判失败(启动卡死,或 loadSpec/i18n 失败弹了模态框——
# 后者正是"包里缺 assets/引擎"类问题的症状,曾漏:mac dmg 没拷 assets)。
# Windows/macOS/Linux 打包流程共用;在 git-bash 下 kill/wait 对它起的进程有效。
# 用法: smoke_launch.sh <可执行文件路径> [超时秒,默认 60]
set -uo pipefail

exe=$1
timeout_s=${2:-60}

export QT_QPA_PLATFORM=offscreen 1F6S_SMOKE_EXIT_MS=3000

"$exe" &
pid=$!
for _ in $(seq 1 "$timeout_s"); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null
    echo "smoke FAIL: $exe 未在 ${timeout_s}s 内自退(卡死或缺资源弹框)" >&2
    exit 1
fi
rc=0
wait "$pid" || rc=$?
if [ "$rc" -ne 0 ]; then
    echo "smoke FAIL: $exe 退出码 $rc" >&2
    exit 1
fi
echo "smoke OK: $exe"
