from __future__ import annotations

import os
import tempfile
import zipfile
from dataclasses import dataclass
from typing import Any, Dict, Optional

from .respack_impl import _parse_info_yml_minimal


@dataclass
class ChartPack:
    root: str
    tmpdir: Optional[tempfile.TemporaryDirectory]
    info: Dict[str, Any]
    chart_path: str
    music_path: str
    bg_path: str


def _read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def load_chart_pack(path: str) -> ChartPack:
    tmp = None
    root = path

    if os.path.isfile(path) and path.lower().endswith((".zip", ".pez")):
        tmp = tempfile.TemporaryDirectory()
        with zipfile.ZipFile(path, "r") as z:
            z.extractall(tmp.name)
        root = tmp.name
    elif os.path.isdir(path):
        root = path
    else:
        raise ValueError(f"Invalid pack path: {path}")

    info_path = os.path.join(root, "info.yml")
    if not os.path.exists(info_path):
        raise ValueError("info.yml not found in chart pack root")

    info = _parse_info_yml_minimal(_read_text(info_path))

    chart_fn = info.get("chart", "chart.json")
    music_fn = info.get("music", "song.mp3")
    bg_fn = info.get("illustration", "background.png")

    return ChartPack(
        root=root,
        tmpdir=tmp,
        info=info,
        chart_path=os.path.join(root, str(chart_fn)),
        music_path=os.path.join(root, str(music_fn)),
        bg_path=os.path.join(root, str(bg_fn)),
    )
