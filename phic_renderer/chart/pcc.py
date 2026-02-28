from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from ..types import RuntimeLine, RuntimeNote
from ..pcc.chart_codec import decode_chart
from ..pcc.container import TYPE_CHRT, TYPE_DICT, TYPE_META, load_chunk, read_pcc


def load_pcc(path: str, W: int, H: int, password: Optional[str] = None) -> Tuple[float, List[RuntimeLine], List[RuntimeNote]]:
    offset, _meta, lines, notes = load_pcc_with_meta(path, W=W, H=H, password=password)
    return offset, lines, notes


def load_pcc_with_meta(
    path: str,
    W: int,
    H: int,
    password: Optional[str] = None,
) -> Tuple[float, Dict[str, Any], List[RuntimeLine], List[RuntimeNote]]:
    pcc = read_pcc(path)
    meta_b = load_chunk(pcc, TYPE_META, password=None) or b''
    dict_b = load_chunk(pcc, TYPE_DICT, password=None) or b''
    chrt_b = load_chunk(pcc, TYPE_CHRT, password=password) or b''

    offset, meta, lines, notes = decode_chart(meta_b, dict_b, chrt_b, W=W, H=H)
    return offset, dict(meta), lines, notes
