#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
import types
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

# Avoid importing phic_renderer/__init__.py (it pulls pygame).
if "phic_renderer" not in sys.modules:
    pkg = types.ModuleType("phic_renderer")
    pkg.__path__ = [str(REPO_ROOT / "phic_renderer")]  # type: ignore[attr-defined]
    sys.modules["phic_renderer"] = pkg
if "phic_renderer.chart" not in sys.modules:
    pkg = types.ModuleType("phic_renderer.chart")
    pkg.__path__ = [str(REPO_ROOT / "phic_renderer" / "chart")]  # type: ignore[attr-defined]
    sys.modules["phic_renderer.chart"] = pkg
if "phic_renderer.engine" not in sys.modules:
    pkg = types.ModuleType("phic_renderer.engine")
    pkg.__path__ = [str(REPO_ROOT / "phic_renderer" / "engine")]  # type: ignore[attr-defined]
    sys.modules["phic_renderer.engine"] = pkg

from phic_renderer.chart.official import load_official  # type: ignore
from phic_renderer.chart.rpe import load_rpe  # type: ignore
from phic_renderer.chart.pec import load_pec_text  # type: ignore
from phic_renderer.engine.mods import apply_mods  # type: ignore


def canonical_notes_python(fmt: str, payload: str, mods: dict[str, Any]) -> list[tuple[int, int, int, float, float, float]]:
    if fmt == "pec":
        _, lines, notes = load_pec_text(payload, 1280, 720)
    else:
        data = json.loads(payload)
        if fmt == "rpe":
            _, lines, notes = load_rpe(data, 1280, 720)
        else:
            _, lines, notes = load_official(data, 1280, 720)
    notes_out = apply_mods(mods, list(notes), list(lines))
    rows: list[tuple[int, int, int, float, float, float]] = []
    for n in notes_out:
        rows.append((int(n.kind), int(bool(n.above)), int(bool(n.fake)), float(n.t_hit), float(n.t_end), float(n.alpha01)))
    rows.sort(key=lambda x: (x[3], x[4], x[0], x[1], x[2], x[5]))
    return rows


def canonical_notes_cpp(exe: Path, fmt: str, chart_path: Path, mods_path: Path) -> list[tuple[int, int, int, float, float, float]]:
    out = subprocess.check_output(
        [str(exe), "--input", str(chart_path), "--format", fmt, "--mods-file", str(mods_path)],
        text=True,
    )
    obj = json.loads(out)
    rows: list[tuple[int, int, int, float, float, float]] = []
    for n in obj.get("notes", []):
        rows.append(
            (
                int(n["kind"]),
                int(bool(n.get("above", False))),
                int(bool(n.get("fake", False))),
                float(n["t_hit"]),
                float(n["hold_end"]),
                float(n.get("alpha", 1.0)),
            )
        )
    rows.sort(key=lambda x: (x[3], x[4], x[0], x[1], x[2], x[5]))
    return rows


def assert_close(a: float, b: float, tol: float, msg: str) -> None:
    if math.fabs(a - b) > tol:
        raise AssertionError(f"{msg}: {a} vs {b} (tol={tol})")


def run_case(exe: Path, name: str, fmt: str, payload: str, mods: dict[str, Any], tol: float = 2e-2) -> None:
    with tempfile.TemporaryDirectory(prefix="phic_oracle_") as td:
        tmp = Path(td)
        chart_ext = "json" if fmt != "pec" else "pec"
        chart_path = tmp / f"case.{chart_ext}"
        mods_path = tmp / "mods.json"
        chart_path.write_text(payload, encoding="utf-8")
        mods_path.write_text(json.dumps(mods), encoding="utf-8")

        py_rows = canonical_notes_python(fmt, payload, mods)
        cpp_rows = canonical_notes_cpp(exe, fmt, chart_path, mods_path)

        if len(py_rows) != len(cpp_rows):
            raise AssertionError(f"{name}: note count mismatch {len(py_rows)} != {len(cpp_rows)}")

        for i, (py_n, cpp_n) in enumerate(zip(py_rows, cpp_rows)):
            if py_n[0] != cpp_n[0]:
                raise AssertionError(f"{name}: kind mismatch at {i}: {py_n[0]} != {cpp_n[0]}")
            if py_n[1] != cpp_n[1]:
                raise AssertionError(f"{name}: above mismatch at {i}: {py_n[1]} != {cpp_n[1]}")
            if py_n[2] != cpp_n[2]:
                raise AssertionError(f"{name}: fake mismatch at {i}: {py_n[2]} != {cpp_n[2]}")
            assert_close(py_n[3], cpp_n[3], tol, f"{name}: t_hit mismatch at {i}")
            assert_close(py_n[4], cpp_n[4], tol, f"{name}: t_end mismatch at {i}")
            assert_close(py_n[5], cpp_n[5], tol, f"{name}: alpha mismatch at {i}")


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: oracle_compare.py <phic_oracle_dump_path>", file=sys.stderr)
        return 2
    exe = Path(sys.argv[1]).resolve()
    if not exe.exists():
        print(f"missing executable: {exe}", file=sys.stderr)
        return 2

    official_payload = json.dumps(
        {
            "name": "official-case",
            "formatVersion": 3,
            "judgeLineList": [
                {
                    "bpm": 120.0,
                    "speedEvents": [{"startTime": 0, "endTime": 256, "value": 1.0}],
                    "judgeLineMoveEvents": [{"startTime": 0, "endTime": 256, "start": 0.5, "end": 0.5, "start2": 0.5, "end2": 0.5}],
                    "judgeLineRotateEvents": [{"startTime": 0, "endTime": 256, "start": 0, "end": 0}],
                    "judgeLineDisappearEvents": [{"startTime": 0, "endTime": 256, "start": 1, "end": 1}],
                    "notesAbove": [
                        {"type": 1, "time": 64, "positionX": -200},
                        {"type": 3, "time": 128, "holdTime": 64, "positionX": 100},
                        {"type": 4, "time": 160, "positionX": 300},
                    ],
                    "notesBelow": [
                        {"type": 2, "time": 96, "positionX": -100},
                    ],
                }
            ],
        }
    )

    rpe_payload = json.dumps(
        {
            "META": {"offset": 0},
            "BPMList": [{"startTime": [0, 0, 1], "bpm": 150}],
            "judgeLineList": [
                {
                    "bpmfactor": 1.0,
                    "eventLayers": [],
                    "notes": [
                        {"type": 1, "startTime": [1, 0, 1], "endTime": [1, 0, 1], "positionX": -0.2},
                        {"type": 2, "startTime": [2, 0, 1], "endTime": [3, 0, 1], "positionX": 0.0},
                        {"type": 3, "startTime": [4, 0, 1], "endTime": [4, 0, 1], "positionX": 0.2},
                        {"type": 4, "startTime": [5, 0, 1], "endTime": [5, 0, 1], "positionX": 0.3},
                    ],
                }
            ],
        }
    )

    cases = [
        ("official_base", "official", official_payload, {}),
        (
            "official_timing_mods",
            "official",
            official_payload,
            {
                "transpose": {"enable": True, "offset": 0.25},
                "stretch": {"enable": True, "factor": 1.2, "anchor": 0.0},
                "quantize": {"enable": True, "time_grid": 0.05},
            },
        ),
        ("official_full_blue", "official", official_payload, {"full_blue": {"enable": True, "convert_non_hold_to_tap": True}}),
        ("official_zip", "official", official_payload, {"compress_zip": {"enable": True, "count": 3}}),
        (
            "official_combo_mods",
            "official",
            official_payload,
            {"stutter": {"enable": True, "count": 2, "delay": 0.01}},
        ),
        (
            "official_attach_fade_rules",
            "official",
            official_payload,
            {
                "attach": {
                    "enable": True,
                    "kind": 4,
                    "lane_offset": 0,
                    "time_offset": 0.03,
                    "side": "flip",
                    "filter": {"kinds": [1]},
                },
                "fade": {
                    "enable": True,
                    "mode": "time",
                    "time_start": 0.0,
                    "time_end": 3.0,
                    "alpha_start": 0.2,
                    "alpha_end": 0.9,
                    "alpha_min": 0.1,
                    "alpha_max": 1.0,
                },
                "note_rules": [
                    {
                        "filter": {"kinds": [4]},
                        "set": {"alpha": 0.7},
                    }
                ],
                "note_overrides": {"set": {"side": "above"}, "apply_to_hold": True},
            },
        ),
        ("rpe_base", "rpe", rpe_payload, {}),
        (
            "rpe_timing_mods",
            "rpe",
            rpe_payload,
            {
                "transpose": {"enable": True, "offset": 0.1},
                "stretch": {"enable": True, "factor": 1.1, "anchor": 0.0},
                "quantize": {"enable": True, "time_grid": 0.04},
            },
        ),
        ("rpe_zip_blue", "rpe", rpe_payload, {"full_blue": {"enable": True}, "compress_zip": {"enable": True, "count": 2}}),
        (
            "rpe_fade_rules",
            "rpe",
            rpe_payload,
            {
                "fade": {"enable": True, "mode": "constant", "constant_alpha": 0.65},
                "note_rules": [{"filter": {"kinds": [1]}, "set": {"kind": 4}}],
            },
        ),
    ]

    for name, fmt, payload, mods in cases:
        run_case(exe, name, fmt, payload, mods)
        print(f"[ok] {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
