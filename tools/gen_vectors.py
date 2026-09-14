#!/usr/bin/env python3
"""黄金向量生成器 — 从网站仓真实代码生成 C++ 对拍 oracle。

必须在网站仓 venv 里运行(cwd 随意,路径内部处理):

    /home/hanlie/hhhermes/1f6s-service-site/.venv/bin/python tools/gen_vectors.py
    # 或: SITE_REPO=/path/to/1f6s-service-site <venv python> tools/gen_vectors.py

原理:sys.path 注入网站仓根目录后,直接调用 app.services.params.resolve /
validate_overrides 与 app.services.compressor.build_commands / estimate_mb,
把输出落盘为 vectors/vectors.json。网站仓的 spec/levels.json 或
compressor.py 改动后必须重跑本脚本,并保证桌面端 C++ 对拍通过。

环境隔离:import 网站模块前先按网站 tests/conftest.py 的做法设置
1F6S_DEV=1 与临时数据目录,避免碰网站仓的 data/(只读 import,不建库)。

向量分三类(kind 字段):
  commands — resolve 成功 → eff + commands(token 数组,路径已占位化)
  error    — resolve 抛 ValueError → expected_error(不生成 commands)
  estimate — estimate_mb 单独采样(不涉及命令构造)

占位化规则:输入路径→INPUT、输出路径→OUTPUT、以 1f6s2pass 结尾的
passlogfile→PASSLOG、os.devnull 的值→DEVNULL;exe 名保持 ffmpeg 原样。
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # 桌面仓根
SITE_REPO = Path(os.environ.get(
    "SITE_REPO", "/home/hanlie/hhhermes/1f6s-service-site"))
OUT_PATH = ROOT / "vectors" / "vectors.json"

DURATION_S = 5940                                       # 99 分钟,规范口径时长
SRC = "/tmp/1f6s-vectors/input.mp4"
DST = "/tmp/1f6s-vectors/output_1f6s.mp4"

# 微调组合矩阵(与网站 params.validate_overrides 白名单对齐)
OVERRIDE_COMBOS: list[dict] = [
    {},
    {"resolution": "720p"},
    {"resolution": "360p"},
    {"interval": 4},
    {"interval": 10},
    {"grayscale": True},
    {"grayscale": False},
    {"audio": "8k"},
    {"audio": "20k"},
    {"target_mb": 8},
    {"target_mb": 1},
    {"target_mb": 500},
    {"resolution": "720p", "interval": 4, "audio": "8k"},
]

# 显式错误用例(记录 expected_error 而非 commands)
ERROR_CASES: list[dict] = [
    {"level": "L7", "overrides": {}, "note": "L7 无 target_mb"},
    {"level": "L4", "overrides": {"resolution": "1080p"}, "note": "resolution 越界"},
    # 表单字段以字符串到达:{"target_mb": "0"} 是 truthy,走 int() 后越界报错
    {"level": "L4", "overrides": {"target_mb": "0"}, "note": "target_mb='0' 下界外"},
    {"level": "L4", "overrides": {"target_mb": 501}, "note": "target_mb 上界外"},
]

# 补充 commands 用例:JSON 整数 target_mb=0 在 validate_overrides 里是 falsy,
# 被当作"继承基础等级"静默丢弃(resolve 成功且 target_mb=None)——这是网站真实
# 行为,C++ 对拍必须复刻,故单独成 case 而非想当然记为错误。
EXTRA_COMMAND_CASES: list[dict] = [
    {"level": "L4", "overrides": {"target_mb": 0},
     "note": "int 0 falsy → 静默丢弃,回到 CRF 模式"},
]

ESTIMATE_DURATIONS = (600, 3600, 5940)
ESTIMATE_TARGET_MBS = (None, 8)


def _isolate_env() -> None:
    """参考网站 tests/conftest.py:数据面隔离到临时目录,dev 模式免 SECRET_KEY。"""
    tmp = Path(tempfile.mkdtemp(prefix="1f6s-genvectors-"))
    os.environ.setdefault("1F6S_DATA_DIR", str(tmp))
    os.environ.setdefault("DATABASE_URL", f"sqlite:///{tmp / 'gen.db'}")
    os.environ.setdefault("1F6S_NO_WORKER", "1")
    os.environ.setdefault("1F6S_DEV", "1")
    os.environ.setdefault("SUPPORT_EMAIL", "support@vectors.local")
    os.environ.setdefault(
        "SECRET_KEY", "vectors-only-secret-key-" + "0123456789abcdef" * 3)


def site_commit() -> str | None:
    try:
        return subprocess.run(
            ["git", "-C", str(SITE_REPO), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, timeout=15,
        ).stdout.strip() or None
    except Exception:
        return None


def override_tag(ov: dict) -> str:
    if not ov:
        return "none"
    return "-".join(f"{k}={ov[k]}" for k in sorted(ov))


def tokenize(cmd: list[str]) -> list[str]:
    out = []
    for tok in cmd:
        if tok == SRC:
            out.append("INPUT")
        elif tok == DST:
            out.append("OUTPUT")
        elif tok.endswith("1f6s2pass"):
            out.append("PASSLOG")
        elif tok == os.devnull:
            out.append("DEVNULL")
        else:
            out.append(tok)
    return out


def build_cases() -> list[dict]:
    from app.services import compressor, params  # 网站仓真实模块(路径已注入)

    levels = [lv["id"] for lv in params.SPEC["levels"]]  # L1..L8,spec 顺序
    cases: list[dict] = []

    # --- 命令矩阵:8 等级 × 微调组合 × has_audio,duration 固定 5940 ---
    # 注:L7 恒要求 target_mb(params.resolve),故 L7 × 无 target_mb 的组合
    # 在这里自然落成 error 用例(expected_error="L7 requires target_mb"),
    # 与显式错误用例互补,均为网站代码的真实行为。
    for level in levels:
        for ov in OVERRIDE_COMBOS:
            for has_audio in (True, False):
                base = {
                    "id": f"{level}|{override_tag(ov)}|audio={str(has_audio).lower()}",
                    "kind": "commands",
                    "level": level,
                    "overrides": ov,
                    "has_audio": has_audio,
                    "duration_s": DURATION_S,
                }
                try:
                    eff = params.resolve(level, dict(ov))
                except ValueError as exc:
                    cases.append({**base, "kind": "error",
                                  "expected_error": str(exc)})
                    continue
                commands = compressor.build_commands(
                    SRC, DST, eff, DURATION_S, has_audio)
                cases.append({**base, "eff": eff,
                              "commands": [tokenize(c) for c in commands]})

    # --- 显式错误用例 ---
    for i, spec in enumerate(ERROR_CASES):
        base = {
            "id": f"err-{i}-{spec['level']}-{override_tag(spec['overrides'])}",
            "kind": "error",
            "level": spec["level"],
            "overrides": spec["overrides"],
            "has_audio": True,
            "duration_s": DURATION_S,
        }
        try:
            params.resolve(spec["level"], dict(spec["overrides"]))
        except ValueError as exc:
            cases.append({**base, "expected_error": str(exc)})
        else:  # pragma: no cover — 网站行为变化时应在此断言失败
            raise AssertionError(
                f"期望 {spec['note']} 抛 ValueError,网站代码却通过了")

    # --- 补充 commands 用例(带 note 说明反直觉行为) ---
    for spec in EXTRA_COMMAND_CASES:
        eff = params.resolve(spec["level"], dict(spec["overrides"]))
        cases.append({
            "id": f"extra-{spec['level']}-{override_tag(spec['overrides'])}",
            "kind": "commands",
            "level": spec["level"],
            "overrides": spec["overrides"],
            "has_audio": True,
            "duration_s": DURATION_S,
            "note": spec["note"],
            "eff": eff,
            "commands": [tokenize(c) for c in compressor.build_commands(
                SRC, DST, eff, DURATION_S, True)],
        })

    # --- estimate_mb 用例:各等级(含 target_mb 生效/不生效)× 时长 ---
    for level in levels:
        for tmb in ESTIMATE_TARGET_MBS:
            for dur in ESTIMATE_DURATIONS:
                cases.append({
                    "id": f"est-{level}|target_mb={tmb if tmb else 'inherit'}|d={dur}",
                    "kind": "estimate",
                    "level": level,
                    "overrides": {} if tmb is None else {"target_mb": tmb},
                    "has_audio": None,
                    "duration_s": dur,
                    "estimate_mb": compressor.estimate_mb(level, dur, tmb),
                })
    return cases


def selfcheck(cases: list[dict]) -> None:
    """抽样自查 3 条(生成即验证,重跑时同样生效)。"""
    by_id = {c["id"]: c for c in cases}

    # 1) L4 无微调 has_audio=true:单命令,-vf 为 fps=1/6,format=gray,scale=-2:480
    c = by_id["L4|none|audio=true"]
    assert len(c["commands"]) == 1, c["id"]
    cmd = c["commands"][0]
    vf = cmd[cmd.index("-vf") + 1]
    assert vf == "fps=1/6,format=gray,scale=-2:480", vf
    assert cmd[0] == "ffmpeg" and "INPUT" in cmd and cmd[-1] == "OUTPUT"
    assert "-tune" in cmd and cmd[cmd.index("-tune") + 1] == "stillimage"
    assert "-crf" in cmd and cmd[cmd.index("-crf") + 1] == "34"
    print(f"  selfcheck 1 OK  L4|none|audio=true -> -vf {vf}")

    # 2) L7 target_mb=8:两条命令,均含 -b:v;pass1 尾 DEVNULL,pass2 尾 OUTPUT
    for suffix, has_audio in (("true", True), ("false", False)):
        c = by_id[f"L7|target_mb=8|audio={suffix}"]
        assert len(c["commands"]) == 2, c["id"]
        p1, p2 = c["commands"]
        for cmd in (p1, p2):
            assert "-b:v" in cmd and "-passlogfile" in cmd \
                and cmd[cmd.index("-passlogfile") + 1] == "PASSLOG", c["id"]
        assert p1[p1.index("-pass") + 1] == "1" and p1[-1] == "DEVNULL"
        assert p2[p2.index("-pass") + 1] == "2" and p2[-1] == "OUTPUT"
        assert c["eff"]["target_mb"] == 8 and c["eff"]["crf"] is None
    print("  selfcheck 2 OK  L7|target_mb=8 -> 2-pass, -b:v, PASSLOG/DEVNULL/OUTPUT")

    # 3) 全部 error 用例:expected_error 非空且无 commands
    errs = [c for c in cases if c["kind"] == "error"]
    assert errs and all(c.get("expected_error") and "commands" not in c
                        for c in errs)
    assert any(c["expected_error"] == "L7 requires target_mb" for c in errs)
    assert any("resolution out of range" == c["expected_error"] for c in errs)
    assert any("target_mb out of range" == c["expected_error"] for c in errs)
    print(f"  selfcheck 3 OK  {len(errs)} error cases carry expected_error")


def main() -> int:
    if not SITE_REPO.is_dir():
        raise SystemExit(f"网站仓不存在: {SITE_REPO}(可用 SITE_REPO=… 覆盖)")
    sys.path.insert(0, str(SITE_REPO))

    _isolate_env()
    from app.services import compressor  # noqa: F401  # 隔离后再 import
    cases = build_cases()
    selfcheck(cases)

    doc = {
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "site_commit": site_commit(),
        "site_repo": str(SITE_REPO),
        "notes": [
            "oracle 由网站仓 app.services.params.resolve + "
            "app.services.compressor.build_commands/estimate_mb 直接生成",
            "token 占位: INPUT/OUTPUT/PASSLOG/DEVNULL;exe 保持 ffmpeg",
            "L7 无 target_mb 的组合按网站真实行为记录为 error 用例",
        ],
        "case_count": len(cases),
        "cases": cases,
    }
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
        f.write("\n")

    kinds = {}
    for c in cases:
        kinds[c["kind"]] = kinds.get(c["kind"], 0) + 1
    print(f"written {OUT_PATH}  ({OUT_PATH.stat().st_size} bytes)")
    print(f"cases: {len(cases)}  by kind: {kinds}")
    print(f"site_commit: {doc['site_commit']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
