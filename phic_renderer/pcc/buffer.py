from __future__ import annotations

import struct
from typing import Optional


class ByteReader:
    def __init__(self, data: bytes):
        self._data = data
        self._mv = memoryview(data)
        self._i = 0

    @property
    def pos(self) -> int:
        return self._i

    def tell(self) -> int:
        return self._i

    def seek(self, pos: int) -> None:
        p = int(pos)
        if p < 0 or p > len(self._data):
            raise ValueError("seek out of range")
        self._i = p

    def read(self, n: int) -> bytes:
        n = int(n)
        if n < 0:
            raise ValueError("read n < 0")
        j = self._i + n
        if j > len(self._data):
            raise ValueError("read past end")
        out = self._mv[self._i : j].tobytes()
        self._i = j
        return out

    def read_u8(self) -> int:
        if self._i + 1 > len(self._data):
            raise ValueError("read past end")
        v = self._mv[self._i]
        self._i += 1
        return int(v)

    def read_u16le(self) -> int:
        return struct.unpack_from('<H', self._mv, self._readn(2))[0]

    def read_u32le(self) -> int:
        return struct.unpack_from('<I', self._mv, self._readn(4))[0]

    def read_u64le(self) -> int:
        return struct.unpack_from('<Q', self._mv, self._readn(8))[0]

    def read_i32le(self) -> int:
        return struct.unpack_from('<i', self._mv, self._readn(4))[0]

    def _readn(self, n: int) -> int:
        n = int(n)
        j = self._i + n
        if j > len(self._data):
            raise ValueError("read past end")
        i0 = self._i
        self._i = j
        return i0


class ByteWriter:
    def __init__(self):
        self._b = bytearray()

    def tell(self) -> int:
        return len(self._b)

    def write(self, bs: bytes) -> None:
        self._b.extend(bs)

    def write_u8(self, v: int) -> None:
        self._b.append(int(v) & 0xFF)

    def write_u16le(self, v: int) -> None:
        self._b.extend(struct.pack('<H', int(v) & 0xFFFF))

    def write_u32le(self, v: int) -> None:
        self._b.extend(struct.pack('<I', int(v) & 0xFFFFFFFF))

    def write_u64le(self, v: int) -> None:
        self._b.extend(struct.pack('<Q', int(v) & 0xFFFFFFFFFFFFFFFF))

    def write_i32le(self, v: int) -> None:
        self._b.extend(struct.pack('<i', int(v)))

    def getvalue(self) -> bytes:
        return bytes(self._b)


def maybe_get_callable_default(fn, idx: int = 0) -> Optional[object]:
    try:
        d = getattr(fn, '__defaults__', None)
        if not d:
            return None
        if idx < 0 or idx >= len(d):
            return None
        return d[idx]
    except Exception:
        return None
