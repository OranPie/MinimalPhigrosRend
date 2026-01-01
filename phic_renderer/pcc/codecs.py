from __future__ import annotations

import zlib
from typing import Optional, Tuple


CODEC_NONE = 0
CODEC_ZLIB = 1
CODEC_ZSTD = 2


def compress_payload(data: bytes, codec_id: int) -> Tuple[bytes, int]:
    cid = int(codec_id)
    if cid == CODEC_NONE:
        return data, CODEC_NONE
    if cid == CODEC_ZLIB:
        return zlib.compress(data, level=9), CODEC_ZLIB
    if cid == CODEC_ZSTD:
        try:
            import zstandard as zstd  # type: ignore
        except Exception:
            return zlib.compress(data, level=9), CODEC_ZLIB
        c = zstd.ZstdCompressor(level=19)
        return c.compress(data), CODEC_ZSTD
    raise ValueError(f"unknown codec_id={cid}")


def decompress_payload(data: bytes, codec_id: int, raw_size: Optional[int] = None) -> bytes:
    cid = int(codec_id)
    if cid == CODEC_NONE:
        return data
    if cid == CODEC_ZLIB:
        return zlib.decompress(data)
    if cid == CODEC_ZSTD:
        import zstandard as zstd  # type: ignore
        d = zstd.ZstdDecompressor()
        if raw_size is None:
            return d.decompress(data)
        return d.decompress(data, max_output_size=int(raw_size))
    raise ValueError(f"unknown codec_id={cid}")
