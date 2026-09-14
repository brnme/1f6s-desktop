#!/usr/bin/env bash
# 同步网站仓规范参数 → 桌面端 assets/spec/。
#
# 唯一参数真相源在网站仓(1f6s-service-site):
#   spec/levels.json — 八级压缩规范参数表(原样拷贝)
#   spec/tiers.json  — 档位/预检参数(本脚本抽取 precheck 节与各档 max_upload_mb)
# 网站侧改了参数,先改网站仓 spec,再跑本脚本同步到桌面端。
#
# 用法: SITE_REPO=/path/to/1f6s-service-site tools/sync_spec.sh
set -euo pipefail

SITE_REPO="${SITE_REPO:-/home/hanlie/hhhermes/1f6s-service-site}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/assets/spec"

mkdir -p "$DEST"

# 1) levels.json 原样拷贝
cp "$SITE_REPO/spec/levels.json" "$DEST/levels.json"

# 2) 用 python3 标准库从 tiers.json 抽取:
#    a) precheck 节整体 → precheck_factors.json(保持 enabled/margin/grace_min/early_kill/factors 结构)
#    b) tiers 各档 max_upload_mb → tiers_limits.json,形如 {"free":2048,"pro":5120,"org":8192}
python3 - "$SITE_REPO/spec/tiers.json" "$DEST" <<'PY'
import json
import sys
from pathlib import Path

tiers_path, dest = Path(sys.argv[1]), Path(sys.argv[2])
with open(tiers_path, encoding="utf-8") as f:
    tiers = json.load(f)

precheck = tiers.get("precheck")
if not precheck:
    raise SystemExit(f"{tiers_path}: 缺少 precheck 节,网站仓 spec 版本过旧?")
with open(dest / "precheck_factors.json", "w", encoding="utf-8") as f:
    json.dump(precheck, f, ensure_ascii=False, indent=2)
    f.write("\n")

limits = {}
for tier_id, tier in tiers.get("tiers", {}).items():
    if "max_upload_mb" not in tier:
        raise SystemExit(f"{tiers_path}: 档位 {tier_id} 缺 max_upload_mb")
    limits[tier_id] = tier["max_upload_mb"]
with open(dest / "tiers_limits.json", "w", encoding="utf-8") as f:
    json.dump(limits, f, ensure_ascii=False, indent=2)
    f.write("\n")
PY

# 3) 汇报产物路径与字节数
for name in levels.json precheck_factors.json tiers_limits.json; do
    p="$DEST/$name"
    printf '%s\t%s bytes\n' "$p" "$(wc -c < "$p")"
done
