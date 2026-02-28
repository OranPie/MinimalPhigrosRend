from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from ..types import RuntimeLine, RuntimeNote
from .chart_codec import encode_chart
from .container import TYPE_CHRT, TYPE_DICT, TYPE_META, write_pcc
from .codecs import CODEC_ZLIB


def save_pcc(
    path: str,
    offset_sec: float,
    lines: List[RuntimeLine],
    notes: List[RuntimeNote],
    W: int,
    H: int,
    meta: Optional[Dict[str, Any]] = None,
    password: Optional[str] = None,
    compress: bool = True,
    encrypt: bool = False,
) -> None:
    meta_b, dict_b, chrt_b = encode_chart(offset_sec, lines, notes, W=W, H=H, meta=meta)

    chunks = [
        (TYPE_META, meta_b, CODEC_ZLIB, False, False),
        (TYPE_DICT, dict_b, CODEC_ZLIB, compress, False),
        (TYPE_CHRT, chrt_b, CODEC_ZLIB, compress, bool(encrypt)),
    ]

    write_pcc(path, chunks, password=password if encrypt else None)


def save_pcc_simple(
    path: str,
    offset_sec: float,
    lines: List[RuntimeLine],
    notes: List[RuntimeNote],
    W: int,
    H: int,
    password: Optional[str] = None,
) -> None:
    save_pcc(path, offset_sec, lines, notes, W=W, H=H, password=password, compress=True, encrypt=bool(password))
