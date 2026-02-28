from __future__ import annotations

import os
from typing import Any, Dict, Optional, Tuple

from ..assets.chartpack import load_chart_pack
from ..assets.loader import load_chart
from .api import save_pcc


def _meta_from_pack_info(info: Dict[str, Any]) -> Dict[str, Any]:
    meta: Dict[str, Any] = {}

    try:
        name = info.get("name", None)
        if name is not None:
            meta["title"] = str(name)
    except Exception:
        pass

    try:
        level = info.get("level", None)
        if level is not None:
            meta["difficulty"] = str(level)
    except Exception:
        pass

    for k_src, k_dst in [
        ("artist", "artist"),
        ("composer", "artist"),
        ("charter", "charter"),
        ("designer", "charter"),
    ]:
        try:
            v = info.get(k_src, None)
            if v is not None and (k_dst not in meta):
                meta[k_dst] = str(v)
        except Exception:
            continue

    return meta


def export_input_to_pcc(
    input_path: str,
    output_path: str,
    W: int,
    H: int,
    password: Optional[str] = None,
    compress: bool = True,
) -> Tuple[str, float, int, int]:
    """Export any supported input chart to PCC.

    Returns (fmt, offset, lines_count, notes_count).
    """

    in_p = str(input_path)
    out_p = str(output_path)

    if not out_p.lower().endswith('.pcc'):
        out_p = out_p + '.pcc'

    meta: Dict[str, Any] = {}

    chart_path = in_p
    pack = None
    if os.path.isdir(in_p) or (os.path.isfile(in_p) and in_p.lower().endswith((".zip", ".pez"))):
        pack = load_chart_pack(in_p)
        chart_path = pack.chart_path
        meta.update(_meta_from_pack_info(pack.info))

    fmt, offset, lines, notes = load_chart(chart_path, int(W), int(H))

    save_pcc(
        out_p,
        float(offset),
        list(lines),
        list(notes),
        W=int(W),
        H=int(H),
        meta=meta,
        password=password,
        compress=bool(compress),
        encrypt=bool(password),
    )

    try:
        if pack and pack.tmpdir is not None:
            pack.tmpdir.cleanup()
    except Exception:
        pass

    return fmt, float(offset), int(len(lines)), int(len(notes))
