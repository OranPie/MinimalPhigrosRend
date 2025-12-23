from __future__ import annotations

import os
import re
import zipfile
import tempfile
import shutil
from dataclasses import dataclass
from typing import Any, Dict, Tuple


@dataclass
class Respack:
    tmpdir: tempfile.TemporaryDirectory
    info: Dict[str, Any]
    img: Dict[str, Any]
    sfx: Dict[str, Any]
    hitfx_sheet: Any
    hitfx_sheet_good: Any
    hitfx_frames_xy: Tuple[int, int]
    hitfx_duration: float
    hitfx_scale: float
    hitfx_rotate: bool
    hitfx_tinted: bool
    hold_tail_h: int
    hold_head_h: int
    hold_tail_h_mh: int
    hold_head_h_mh: int
    hold_repeat: bool
    hold_compact: bool
    hold_keep_head: bool
    hold_tail_no_scale: bool
    hide_particles: bool
    judge_colors: Dict[str, Tuple[int, int, int, int]]


def _parse_hex_rgba(v: Any, default: Tuple[int, int, int, int]) -> Tuple[int, int, int, int]:
    if v is None:
        return default
    try:
        if isinstance(v, int):
            n = v
        else:
            s = str(v).strip()
            n = int(s, 0)
        if n <= 0xFFFFFF:
            r = (n >> 16) & 255
            g = (n >> 8) & 255
            b = n & 255
            a = 255
            return r, g, b, a
        # respack commonly uses AARRGGBB
        a = (n >> 24) & 255
        r = (n >> 16) & 255
        g = (n >> 8) & 255
        b = n & 255
        return r, g, b, a
    except:
        return default


def _parse_info_yml_minimal(text: str) -> Dict[str, Any]:
    out: Dict[str, Any] = {}

    def _strip_inline_comment(s: str) -> str:
        in_sq = False
        in_dq = False
        buf = []
        for ch in s:
            if ch == "'" and (not in_dq):
                in_sq = not in_sq
            elif ch == '"' and (not in_sq):
                in_dq = not in_dq
            if (not in_sq) and (not in_dq) and ch == "#":
                break
            buf.append(ch)
        return "".join(buf).rstrip()

    for raw in text.splitlines():
        line = _strip_inline_comment(raw).strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        k = k.strip()
        v = v.strip().strip('"').strip("'")
        if v.startswith("[") and v.endswith("]"):
            inside = v[1:-1].strip()
            if inside:
                parts = [p.strip() for p in inside.split(",")]
                arr = []
                for p in parts:
                    try:
                        arr.append(int(p))
                    except:
                        try:
                            arr.append(float(p))
                        except:
                            arr.append(p.strip('"').strip("'"))
                out[k] = arr
            else:
                out[k] = []
        else:
            if v.lower() in ("true", "false"):
                out[k] = v.lower() == "true"
            else:
                try:
                    out[k] = int(v)
                except:
                    try:
                        out[k] = float(v)
                    except:
                        out[k] = v
    return out


def load_respack_info(zip_path: str) -> Tuple[tempfile.TemporaryDirectory, Dict[str, Any]]:
    tmpdir = tempfile.TemporaryDirectory()
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(tmpdir.name)
    info_path = os.path.join(tmpdir.name, "info.yml")
    with open(info_path, "r", encoding="utf-8") as f:
        info = _parse_info_yml_minimal(f.read())
    return tmpdir, info
