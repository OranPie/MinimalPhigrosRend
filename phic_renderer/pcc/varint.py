from __future__ import annotations

from typing import Tuple


def encode_uvarint(x: int) -> bytes:
    x = int(x)
    if x < 0:
        raise ValueError("uvarint must be non-negative")
    out = bytearray()
    while True:
        b = x & 0x7F
        x >>= 7
        if x:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def decode_uvarint(buf: bytes, off: int = 0) -> Tuple[int, int]:
    x = 0
    shift = 0
    i = int(off)
    n = len(buf)
    while True:
        if i >= n:
            raise ValueError("uvarint truncated")
        b = buf[i]
        i += 1
        x |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
        if shift > 63:
            raise ValueError("uvarint too large")
    return x, i


def zigzag_encode(x: int) -> int:
    x = int(x)
    return (x << 1) ^ (x >> 63)


def zigzag_decode(x: int) -> int:
    x = int(x)
    return (x >> 1) ^ (-(x & 1))


def encode_svarint(x: int) -> bytes:
    return encode_uvarint(zigzag_encode(int(x)))


def decode_svarint(buf: bytes, off: int = 0) -> Tuple[int, int]:
    ux, i = decode_uvarint(buf, off)
    return zigzag_decode(ux), i
