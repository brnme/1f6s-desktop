#!/usr/bin/env bash
# 同步网站仓 i18n 词条 → 桌面端 assets/i18n/{en,zh}.json。
#
# 词条两个来源:
#   1) 网站仓 app/locales/{en,zh}.json —— 抽取下方 REQUIRED_SITE_KEYS 子集
#      (等级名/场景与提示/预期体积、三模式文案、目标体积、红线警示等);
#   2) assets/i18n/desktop.{en,zh}.json —— 桌面专属 UI 文案(两语手写,
#      缺网站词条时的兜底也写在这里)。
# 合并结果写入 assets/i18n/{en,zh}.json —— **生成物,勿手编**;改动请改
# desktop.{en,zh}.json 或网站仓 locales 后重跑本脚本。key 两语必须对齐
# (tests/test_i18n 有 parity 断言)。
#
# 用法: SITE_REPO=/path/to/1f6s-service-site tools/sync_i18n.sh
set -euo pipefail

SITE_REPO="${SITE_REPO:-/home/hanlie/hhhermes/1f6s-service-site}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/assets/i18n"

mkdir -p "$DEST"

python3 - "$SITE_REPO" "$DEST" <<'PY'
import json
import sys
from pathlib import Path

site_repo, dest = Path(sys.argv[1]), Path(sys.argv[2])

# 从网站 locales 抽取的子集 key(与网站 app/templates/compress.html 引用一致)。
# 网站改 key 名时这里要同步改;网站缺某个 key 时由 desktop 文件兜底。
LEVELS = [f"L{i}" for i in range(1, 9)]
SCENARIOS = ["email", "im", "training", "mooc", "demo",
             "compliance", "offline", "platform", "archive"]  # presets.py 插入序
REQUIRED_SITE_KEYS = (
    [f"level.{lv}.{f}" for lv in LEVELS for f in ("name", "scene", "expected")]
    + [f"scenario.{sc}{sfx}" for sc in SCENARIOS for sfx in ("", ".hint")]
    + ["upload.mode.level", "upload.mode.level.hint",
       "upload.mode.scenario", "upload.mode.scenario.hint",
       "upload.mode.finetune", "upload.mode.finetune.hint",
       "upload.level", "upload.scenario",
       "upload.target_mb", "upload.target_mb.hint",
       "upload.redline.title", "upload.redline.body",
       "upload.finetune.base", "upload.finetune.resolution",
       "upload.finetune.interval", "upload.finetune.grayscale",
       "upload.finetune.grayscale.on", "upload.finetune.grayscale.off",
       "upload.finetune.audio"]
)


def load(path: Path) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


site = {loc: load(site_repo / "app" / "locales" / f"{loc}.json")
        for loc in ("en", "zh")}
desktop = {loc: load(dest / f"desktop.{loc}.json") for loc in ("en", "zh")}

# desktop 源文件自身也要两语对齐(脚本内先拦一次,parity 细节归 test_i18n)。
if set(desktop["en"]) != set(desktop["zh"]):
    raise SystemExit("desktop.en.json 与 desktop.zh.json key 不对齐:"
                     f"仅 en 有 {sorted(set(desktop['en']) - set(desktop['zh']))},"
                     f"仅 zh 有 {sorted(set(desktop['zh']) - set(desktop['en']))}")

for loc in ("en", "zh"):
    out = {}
    for key in REQUIRED_SITE_KEYS:
        if key in site[loc]:
            out[key] = site[loc][key]
        elif key in desktop[loc]:
            print(f"warn: 网站缺 {key},用桌面兜底词条")
            out[key] = desktop[loc][key]
        else:
            raise SystemExit(f"{loc}: 网站与桌面都没有词条 {key}")
    # 桌面专属 key 全量并入(排除已用作兜底的)。
    for key, val in desktop[loc].items():
        if key in out and key not in REQUIRED_SITE_KEYS:
            raise SystemExit(f"desktop.{loc}.json 与网站词条撞名:{key}")
        out.setdefault(key, val)
    with open(dest / f"{loc}.json", "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2, sort_keys=True)
        f.write("\n")
    print(f"{dest / f'{loc}.json'}\t{len(out)} keys")
PY
