from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

from . import PCC_MAGIC, PCC_VER_MAJOR, PCC_VER_MINOR
from .buffer import ByteReader, ByteWriter
from .codecs import CODEC_NONE, compress_payload, decompress_payload
from .crypto import CRYPT_NONE, CryptoParams, default_params, decrypt_aead, encrypt_aead


@dataclass
class ChunkInfo:
    chunk_type: int
    chunk_flags: int
    codec_id: int
    crypt_id: int
    raw_size: int
    stored_size: int
    offset: int


def _crc32(data: bytes) -> int:
    return int(binascii.crc32(data) & 0xFFFFFFFF)


def _fourcc(s: str) -> int:
    b = s.encode('ascii')
    if len(b) != 4:
        raise ValueError("fourcc must be 4 bytes")
    return struct.unpack('<I', b)[0]


TYPE_META = _fourcc('META')
TYPE_DICT = _fourcc('DICT')
TYPE_CHRT = _fourcc('CHRT')
TYPE_CRYP = _fourcc('CRYP')


FLAG_CHUNK_COMPRESSED = 1 << 0
FLAG_CHUNK_ENCRYPTED = 1 << 1


@dataclass
class PCCFile:
    header_flags: int
    chunks: List[ChunkInfo]
    data: bytes

    def find(self, chunk_type: int) -> Optional[ChunkInfo]:
        for c in self.chunks:
            if int(c.chunk_type) == int(chunk_type):
                return c
        return None


def read_pcc(path: str) -> PCCFile:
    with open(path, 'rb') as f:
        data = f.read()

    if len(data) < 32:
        raise ValueError('PCC file too small')

    r = ByteReader(data)
    magic = r.read(4)
    if magic != PCC_MAGIC:
        raise ValueError('Invalid PCC magic')

    ver_major = r.read_u8()
    ver_minor = r.read_u8()
    header_flags = r.read_u16le()
    header_len = r.read_u16le()
    chunk_count = r.read_u16le()
    toc_off = r.read_u32le()
    meta_off = r.read_u32le()
    file_size = r.read_u64le()
    hdr_crc = r.read_u32le()

    if int(file_size) != len(data):
        raise ValueError('PCC file_size mismatch')

    hdr = data[: int(header_len)]
    hdr2 = bytearray(hdr)
    if len(hdr2) >= 32:
        struct.pack_into('<I', hdr2, 28, 0)
    if _crc32(bytes(hdr2)) != int(hdr_crc):
        raise ValueError('PCC header CRC mismatch')

    if ver_major != PCC_VER_MAJOR:
        raise ValueError(f'Unsupported PCC major version {ver_major}')

    if toc_off <= 0 or toc_off >= len(data):
        raise ValueError('Invalid PCC toc_offset')

    r.seek(toc_off)
    chunks: List[ChunkInfo] = []
    for _ in range(int(chunk_count)):
        ct = r.read_u32le()
        off = r.read_u64le()
        stored = r.read_u32le()
        raw = r.read_u32le()
        codec_id = r.read_u8()
        crypt_id = r.read_u8()
        cflags = r.read_u16le()
        chunks.append(ChunkInfo(ct, cflags, codec_id, crypt_id, raw, stored, off))

    return PCCFile(header_flags=header_flags, chunks=chunks, data=data)


def _read_chunk_payload(pcc: PCCFile, info: ChunkInfo) -> bytes:
    off = int(info.offset)
    end = off + int(info.stored_size)
    if off < 0 or end > len(pcc.data):
        raise ValueError('Chunk out of range')
    return pcc.data[off:end]


def load_chunk(pcc: PCCFile, chunk_type: int, password: Optional[str] = None) -> Optional[bytes]:
    info = pcc.find(chunk_type)
    if info is None:
        return None

    payload = _read_chunk_payload(pcc, info)

    if info.chunk_flags & FLAG_CHUNK_ENCRYPTED:
        if not password:
            raise RuntimeError('PCC chunk is encrypted but no password provided')
        cinfo = pcc.find(TYPE_CRYP)
        if cinfo is None:
            raise ValueError('Encrypted PCC missing CRYP chunk')
        cpay = _read_chunk_payload(pcc, cinfo)
        params = parse_cryp_chunk(cpay)

        if len(payload) < 12:
            raise ValueError('Encrypted chunk too small')
        nonce = payload[:12]
        ct = payload[12:]
        aad = build_aad(info)
        payload = decrypt_aead(password, params, nonce, ct, aad)

    if info.chunk_flags & FLAG_CHUNK_COMPRESSED:
        payload = decompress_payload(payload, info.codec_id, raw_size=info.raw_size)

    if int(info.raw_size) != len(payload):
        raise ValueError('Chunk raw_size mismatch')

    return payload


def build_aad(info: ChunkInfo) -> bytes:
    # NOTE: AAD must be stable across write/read. Do NOT include offset/stored_size because
    # encryption modifies stored_size (nonce+tag), causing mismatch.
    return struct.pack(
        '<I H B B I',
        int(info.chunk_type) & 0xFFFFFFFF,
        int(info.chunk_flags) & 0xFFFF,
        int(info.codec_id) & 0xFF,
        int(info.crypt_id) & 0xFF,
        int(info.raw_size) & 0xFFFFFFFF,
    )


def parse_cryp_chunk(data: bytes) -> CryptoParams:
    r = ByteReader(data)
    crypt_id = r.read_u8()
    salt_len = r.read_u8()
    salt = r.read(salt_len)
    n = r.read_u32le()
    rr = r.read_u32le()
    pp = r.read_u32le()
    return CryptoParams(crypt_id=int(crypt_id), salt=salt, n=int(n), r=int(rr), p=int(pp))


def build_cryp_chunk(params: CryptoParams) -> bytes:
    w = ByteWriter()
    w.write_u8(int(params.crypt_id))
    w.write_u8(len(params.salt))
    w.write(params.salt)
    w.write_u32le(int(params.n))
    w.write_u32le(int(params.r))
    w.write_u32le(int(params.p))
    return w.getvalue()


def write_pcc(
    path: str,
    chunks: List[Tuple[int, bytes, int, bool, bool]],
    password: Optional[str] = None,
    crypto_params: Optional[CryptoParams] = None,
) -> None:
    """chunks: list of (chunk_type, raw_payload, codec_id, compress, encrypt)."""

    if crypto_params is None:
        crypto_params = default_params()

    out_chunks: List[ChunkInfo] = []
    payloads: List[bytes] = []

    header_len = 64

    # placeholder header; patched later
    w = ByteWriter()
    w.write(PCC_MAGIC)
    w.write_u8(PCC_VER_MAJOR)
    w.write_u8(PCC_VER_MINOR)

    header_flags = 0
    if password:
        header_flags |= 1
    w.write_u16le(header_flags)
    w.write_u16le(header_len)
    w.write_u16le(len(chunks) + (1 if password else 0))
    w.write_u32le(0)  # toc_offset
    w.write_u32le(0)  # meta_offset
    w.write_u64le(0)  # file_size
    w.write_u32le(0)  # header_crc32

    # pad to header_len
    if w.tell() > header_len:
        raise ValueError('header too small')
    w.write(b'\x00' * (header_len - w.tell()))

    # optional CRYP chunk
    if password:
        cryp = build_cryp_chunk(crypto_params)
        payloads.append(cryp)
        out_chunks.append(ChunkInfo(TYPE_CRYP, 0, CODEC_NONE, CRYPT_NONE, len(cryp), len(cryp), w.tell()))
        w.write(cryp)

    # normal chunks
    for (ctype, raw, codec_id, do_comp, do_enc) in chunks:
        cflags = 0
        payload = raw
        if do_comp:
            payload, used = compress_payload(payload, codec_id)
            codec_id = used
            cflags |= FLAG_CHUNK_COMPRESSED
        if do_enc:
            if not password:
                raise ValueError('encrypt requested but password is None')
            cflags |= FLAG_CHUNK_ENCRYPTED
            aad = build_aad(ChunkInfo(int(ctype), cflags, int(codec_id), int(crypto_params.crypt_id), len(raw), len(payload), w.tell()))
            nonce, ct = encrypt_aead(password, crypto_params, payload, aad)
            payload = nonce + ct
        out_chunks.append(ChunkInfo(int(ctype), int(cflags), int(codec_id), int(crypto_params.crypt_id if do_enc else CRYPT_NONE), len(raw), len(payload), w.tell()))
        payloads.append(payload)
        w.write(payload)

    toc_offset = w.tell()

    # TOC
    for ci in out_chunks:
        w.write_u32le(int(ci.chunk_type))
        w.write_u64le(int(ci.offset))
        w.write_u32le(int(ci.stored_size))
        w.write_u32le(int(ci.raw_size))
        w.write_u8(int(ci.codec_id))
        w.write_u8(int(ci.crypt_id))
        w.write_u16le(int(ci.chunk_flags))

    file_bytes = w.getvalue()

    # patch header fields
    fb = bytearray(file_bytes)
    struct.pack_into('<I', fb, 12, int(toc_offset))

    meta_off = 0
    for ci in out_chunks:
        if ci.chunk_type == TYPE_META:
            meta_off = int(ci.offset)
            break
    struct.pack_into('<I', fb, 16, int(meta_off))

    struct.pack_into('<Q', fb, 20, int(len(fb)))

    hdr = bytes(fb[:header_len])
    hdr2 = bytearray(hdr)
    struct.pack_into('<I', hdr2, 28, 0)
    crc = _crc32(bytes(hdr2))
    struct.pack_into('<I', fb, 28, int(crc))

    with open(path, 'wb') as f:
        f.write(bytes(fb))
