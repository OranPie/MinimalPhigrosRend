from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from ..math.easing import ease_01, easing_from_type
from ..math.tracks import ColorSeg, EasedSeg, IntegralTrack, PiecewiseColor, PiecewiseEased, PiecewiseText, Seg1D, TextSeg
from ..types import RuntimeLine, RuntimeNote
from .buffer import ByteReader, ByteWriter
from .varint import decode_svarint, decode_uvarint, encode_svarint, encode_uvarint


TICK_HZ = 1000  # 1 tick = 1ms


def sec_to_tick(t: float) -> int:
    return int(round(float(t) * TICK_HZ))


def tick_to_sec(tick: int) -> float:
    return float(int(tick)) / float(TICK_HZ)


def q_u16_01(x01: float) -> int:
    v = int(round(float(x01) * 65535.0))
    return 0 if v < 0 else (65535 if v > 65535 else v)


def dq_u16_01(q: int) -> float:
    return float(int(q)) / 65535.0


def q_s14_norm(x: float) -> int:
    return int(round(float(x) * 16384.0))


def dq_s14_norm(q: int) -> float:
    return float(int(q)) / 16384.0


def q_rot(rad: float) -> int:
    return int(round(float(rad) * 4096.0))


def dq_rot(q: int) -> float:
    return float(int(q)) / 4096.0


def q_scale(x: float) -> int:
    return int(round(float(x) * 4096.0))


def dq_scale(q: int) -> float:
    return float(int(q)) / 4096.0


def q_mul256(x: float) -> int:
    return int(round(float(x) * 256.0))


def dq_mul256(q: int) -> float:
    return float(int(q)) / 256.0


def q_alpha01(x01: float) -> int:
    v = int(round(float(x01) * 255.0))
    return 0 if v < 0 else (255 if v > 255 else v)


def dq_alpha01(q: int) -> float:
    return float(int(q)) / 255.0


def encode_dict(strings: List[str]) -> bytes:
    w = ByteWriter()
    w.write(encode_uvarint(len(strings)))
    for s in strings:
        b = s.encode('utf-8')
        w.write(encode_uvarint(len(b)))
        w.write(b)
    return w.getvalue()


def decode_dict(data: bytes) -> List[str]:
    r = ByteReader(data)
    n, _ = decode_uvarint(data, r.pos)
    r.seek(_)
    out: List[str] = []
    for _ in range(int(n)):
        ln, j = decode_uvarint(data, r.pos)
        r.seek(j)
        out.append(r.read(int(ln)).decode('utf-8', errors='replace'))
    return out


def _dict_id(s: Optional[str], pool: Dict[str, int], out: List[str]) -> int:
    if not s:
        return 0
    if s in pool:
        return int(pool[s])
    out.append(str(s))
    idx = len(out)
    pool[str(s)] = idx
    return idx


def encode_meta(meta: Dict[str, object], dict_pool: Dict[str, int], dict_list: List[str]) -> bytes:
    w = ByteWriter()

    # tags: 1 title,2 artist,3 charter,4 difficulty,5 offset_ms
    title = _dict_id(str(meta.get('title', '') or ''), dict_pool, dict_list)
    artist = _dict_id(str(meta.get('artist', '') or ''), dict_pool, dict_list)
    charter = _dict_id(str(meta.get('charter', '') or ''), dict_pool, dict_list)
    diff = _dict_id(str(meta.get('difficulty', '') or ''), dict_pool, dict_list)

    w.write(encode_uvarint(1)); w.write(encode_uvarint(title))
    w.write(encode_uvarint(2)); w.write(encode_uvarint(artist))
    w.write(encode_uvarint(3)); w.write(encode_uvarint(charter))
    w.write(encode_uvarint(4)); w.write(encode_uvarint(diff))

    off_ms = int(round(float(meta.get('offset_ms', 0) or 0)))
    w.write(encode_uvarint(5)); w.write_i32le(off_ms)

    w.write(encode_uvarint(0))
    return w.getvalue()


def decode_meta(data: bytes, dict_list: List[str]) -> Dict[str, object]:
    r = ByteReader(data)
    out: Dict[str, object] = {}
    while True:
        tag, j = decode_uvarint(data, r.pos)
        r.seek(j)
        if int(tag) == 0:
            break
        if int(tag) in (1, 2, 3, 4):
            sid, j2 = decode_uvarint(data, r.pos)
            r.seek(j2)
            s = dict_list[int(sid) - 1] if int(sid) > 0 and int(sid) <= len(dict_list) else ''
            if int(tag) == 1:
                out['title'] = s
            elif int(tag) == 2:
                out['artist'] = s
            elif int(tag) == 3:
                out['charter'] = s
            else:
                out['difficulty'] = s
            continue
        if int(tag) == 5:
            out['offset_ms'] = r.read_i32le()
            continue
        raise ValueError(f'Unknown META tag {tag}')
    return out


def _extract_track(obj) -> Optional[PiecewiseEased]:
    if isinstance(obj, PiecewiseEased):
        return obj
    try:
        if hasattr(obj, 'segs') and hasattr(obj, 'eval'):
            return obj
    except Exception:
        pass
    return None


def _extract_color_track(obj) -> Optional[PiecewiseColor]:
    if isinstance(obj, PiecewiseColor):
        return obj
    try:
        if hasattr(obj, 'segs') and hasattr(obj, 'eval'):
            return obj
    except Exception:
        pass
    return None


def _extract_text_track(obj) -> Optional[PiecewiseText]:
    if isinstance(obj, PiecewiseText):
        return obj
    try:
        if hasattr(obj, 'segs') and hasattr(obj, 'eval'):
            return obj
    except Exception:
        pass
    return None


def _track_to_bytes(track: PiecewiseEased, qv, qdv=None, default_v: float = 0.0) -> bytes:
    w = ByteWriter()
    w.write(encode_svarint(qv(getattr(track, 'default', default_v))))
    segs = list(getattr(track, 'segs', []) or [])
    w.write(encode_uvarint(len(segs)))
    last_t = 0
    for s in segs:
        t0 = sec_to_tick(float(s.t0))
        t1 = sec_to_tick(float(s.t1))
        dt0 = max(0, int(t0) - int(last_t))
        dur = max(0, int(t1) - int(t0))
        w.write(encode_uvarint(dt0))
        w.write(encode_uvarint(dur))
        v0q = int(qv(float(s.v0)))
        v1q = int(qv(float(s.v1)))
        w.write(encode_svarint(v0q))
        w.write(encode_svarint(int(v1q - v0q)))
        eid = 0
        try:
            fn = s.easing
            name = getattr(fn, '__name__', '')
            if name.startswith('ease_'):
                eid = int(name.split('_')[1])
                if eid > 0:
                    eid -= 1
        except Exception:
            eid = 0
        w.write_u8(eid & 0xFF)
        L = int(round(float(getattr(s, 'L', 0.0)) * 255.0))
        R = int(round(float(getattr(s, 'R', 1.0)) * 255.0))
        if L < 0:
            L = 0
        if L > 255:
            L = 255
        if R < 0:
            R = 0
        if R > 255:
            R = 255
        w.write_u8(L)
        w.write_u8(R)
        last_t = t1
    return w.getvalue()


def _bytes_to_track(data: bytes, dqv, default_v: float = 0.0) -> PiecewiseEased:
    r = ByteReader(data)
    dv, j = decode_svarint(data, r.pos)
    r.seek(j)
    default = dqv(int(dv))
    n, j2 = decode_uvarint(data, r.pos)
    r.seek(j2)
    segs: List[EasedSeg] = []
    t_cur = 0
    for _ in range(int(n)):
        dt0, j = decode_uvarint(data, r.pos)
        r.seek(j)
        dur, j = decode_uvarint(data, r.pos)
        r.seek(j)
        v0q, j = decode_svarint(data, r.pos)
        r.seek(j)
        dvq, j = decode_svarint(data, r.pos)
        r.seek(j)
        eid = r.read_u8()
        L = r.read_u8()
        R = r.read_u8()
        t0 = t_cur + int(dt0)
        t1 = t0 + int(dur)
        v0 = dqv(int(v0q))
        v1 = dqv(int(v0q + dvq))
        segs.append(EasedSeg(tick_to_sec(t0), tick_to_sec(t1), float(v0), float(v1), easing_from_type(int(eid)), float(L) / 255.0, float(R) / 255.0))
        t_cur = t1
    return PiecewiseEased(segs, default=float(default))


def _integral_to_bytes(track: IntegralTrack, qv) -> bytes:
    w = ByteWriter()
    segs = list(getattr(track, 'segs', []) or [])
    w.write(encode_uvarint(len(segs)))
    last_t = 0
    for s in segs:
        t0 = sec_to_tick(float(s.t0))
        t1 = sec_to_tick(float(s.t1))
        dt0 = max(0, int(t0) - int(last_t))
        dur = max(0, int(t1) - int(t0))
        w.write(encode_uvarint(dt0))
        w.write(encode_uvarint(dur))
        v0q = int(qv(float(s.v0)))
        v1q = int(qv(float(s.v1)))
        w.write(encode_svarint(v0q))
        w.write(encode_svarint(int(v1q - v0q)))
        last_t = t1
    return w.getvalue()


def _bytes_to_integral(data: bytes, dqv) -> IntegralTrack:
    r = ByteReader(data)
    n, j = decode_uvarint(data, r.pos)
    r.seek(j)
    segs: List[Seg1D] = []
    t_cur = 0
    prefix = 0.0
    for _ in range(int(n)):
        dt0, j = decode_uvarint(data, r.pos)
        r.seek(j)
        dur, j = decode_uvarint(data, r.pos)
        r.seek(j)
        v0q, j = decode_svarint(data, r.pos)
        r.seek(j)
        dvq, j = decode_svarint(data, r.pos)
        r.seek(j)
        t0 = t_cur + int(dt0)
        t1 = t0 + int(dur)
        v0 = float(dqv(int(v0q)))
        v1 = float(dqv(int(v0q + dvq)))
        segs.append(Seg1D(tick_to_sec(t0), tick_to_sec(t1), v0, v1, prefix))
        dt = tick_to_sec(t1) - tick_to_sec(t0)
        prefix += 0.5 * (v0 + v1) * dt
        t_cur = t1
    return IntegralTrack(segs)


def encode_chart(
    offset_sec: float,
    lines: List[RuntimeLine],
    notes: List[RuntimeNote],
    W: int,
    H: int,
    meta: Optional[Dict[str, object]] = None,
) -> Tuple[bytes, bytes, bytes]:
    dict_pool: Dict[str, int] = {}
    dict_list: List[str] = []

    m = meta or {}
    m = dict(m)
    m['offset_ms'] = int(round(float(offset_sec) * 1000.0))

    meta_bytes = encode_meta(m, dict_pool, dict_list)

    if int(W) <= 0 or int(H) <= 0:
        raise ValueError('PCC encode_chart requires positive W/H')

    w = ByteWriter()
    w.write(encode_uvarint(0))
    w.write(encode_uvarint(len(lines)))
    w.write(encode_uvarint(len(notes)))

    def _get_eval_fn(obj: Any):
        if obj is None:
            return None
        if hasattr(obj, 'eval'):
            return lambda t, o=obj: float(o.eval(t))
        if callable(obj):
            return lambda t, o=obj: float(o(t))
        return None

    # Build a compact sampling time grid for exporting non-piecewise tracks.
    # Use scroll segment boundaries and note times; this keeps size reasonable while capturing structure.
    times_set = set()
    times_set.add(0.0)
    t_end_hint = 0.0
    for ln in lines:
        sc = getattr(ln, 'scroll_px', None)
        if isinstance(sc, IntegralTrack):
            for s in (sc.segs or []):
                try:
                    times_set.add(float(s.t0))
                    times_set.add(float(s.t1))
                    t_end_hint = max(t_end_hint, float(s.t1))
                except Exception:
                    continue
    for n in notes:
        try:
            times_set.add(float(n.t_hit))
            times_set.add(float(n.t_end))
            t_end_hint = max(t_end_hint, float(n.t_end))
        except Exception:
            continue
    if t_end_hint > 0.0:
        times_set.add(float(t_end_hint))

    times = sorted(times_set)
    if len(times) < 2:
        times = [0.0, max(1.0, float(t_end_hint))]

    def _linear_piecewise(fn, div: float) -> PiecewiseEased:
        segs: List[EasedSeg] = []
        try:
            default = fn(times[0]) / div
        except Exception:
            default = 0.0
        for t0, t1 in zip(times[:-1], times[1:]):
            if float(t1) <= float(t0) + 1e-9:
                continue
            v0 = fn(t0) / div
            v1 = fn(t1) / div
            segs.append(EasedSeg(float(t0), float(t1), float(v0), float(v1), ease_01, 0.0, 1.0))
        return PiecewiseEased(segs, default=float(default))

    def _ensure_piecewise_norm(track_or_fn: Any, div: float) -> PiecewiseEased:
        if isinstance(track_or_fn, PiecewiseEased):
            segs = [EasedSeg(s.t0, s.t1, s.v0 / div, s.v1 / div, s.easing, s.L, s.R) for s in (track_or_fn.segs or [])]
            return PiecewiseEased(segs, default=float(track_or_fn.default) / div)
        fn = _get_eval_fn(track_or_fn)
        if fn is None:
            return PiecewiseEased([], default=0.0)
        return _linear_piecewise(fn, div)

    def _ensure_piecewise(track_or_fn: Any) -> PiecewiseEased:
        if isinstance(track_or_fn, PiecewiseEased):
            return track_or_fn
        fn = _get_eval_fn(track_or_fn)
        if fn is None:
            return PiecewiseEased([], default=0.0)
        return _linear_piecewise(fn, 1.0)

    def _write_blob(blob: bytes) -> None:
        w.write(encode_uvarint(len(blob)))
        w.write(blob)

    for ln in lines:
        name_id = _dict_id(getattr(ln, 'name', '') or '', dict_pool, dict_list)
        tex_id = _dict_id(getattr(ln, 'texture_path', None), dict_pool, dict_list)

        w.write(encode_uvarint(name_id))
        w.write(encode_uvarint(int(getattr(ln, 'father', -1)) + 1))
        w.write_u8(1 if bool(getattr(ln, 'rotate_with_father', True)) else 0)
        w.write(encode_uvarint(tex_id))

        ax, ay = getattr(ln, 'anchor', (0.5, 0.5))
        w.write_u16le(q_u16_01(float(ax)))
        w.write_u16le(q_u16_01(float(ay)))
        w.write_u8(1 if bool(getattr(ln, 'is_gif', False)) else 0)

        # Encode as normalized tracks.
        tx = _ensure_piecewise_norm(getattr(ln, 'pos_x', None), float(W))
        ty = _ensure_piecewise_norm(getattr(ln, 'pos_y', None), float(H))
        tr = _ensure_piecewise(getattr(ln, 'rot', None))
        ta = _ensure_piecewise(getattr(ln, 'alpha', None))

        _write_blob(_track_to_bytes(tx, q_s14_norm))
        _write_blob(_track_to_bytes(ty, q_s14_norm))
        _write_blob(_track_to_bytes(tr, q_rot))
        _write_blob(_track_to_bytes(ta, q_alpha01))

        scroll = getattr(ln, 'scroll_px', None)
        if not isinstance(scroll, IntegralTrack):
            raise ValueError('PCC encode_chart expects scroll_px to be IntegralTrack')
        # normalize scroll speed (px/sec) by H
        segs = [Seg1D(s.t0, s.t1, s.v0 / float(H), s.v1 / float(H), s.prefix / float(H)) for s in (scroll.segs or [])]
        scroll_norm = IntegralTrack(segs)
        _write_blob(_integral_to_bytes(scroll_norm, q_s14_norm))

        # extended optional tracks
        col = _extract_color_track(getattr(ln, 'color', None))
        if col is None:
            w.write_u8(0)
        else:
            w.write_u8(1)
            segs = list(getattr(col, 'segs', []) or [])
            ww = ByteWriter()
            ww.write(encode_uvarint(len(segs)))
            last_t = 0
            for s in segs:
                t0 = sec_to_tick(float(s.t0))
                t1 = sec_to_tick(float(s.t1))
                ww.write(encode_uvarint(max(0, t0 - last_t)))
                ww.write(encode_uvarint(max(0, t1 - t0)))
                ww.write_u8(int(s.c0[0]) & 255); ww.write_u8(int(s.c0[1]) & 255); ww.write_u8(int(s.c0[2]) & 255)
                ww.write_u8(int(s.c1[0]) & 255); ww.write_u8(int(s.c1[1]) & 255); ww.write_u8(int(s.c1[2]) & 255)
                eid = 0
                try:
                    fn = s.easing
                    name = getattr(fn, '__name__', '')
                    if name.startswith('ease_'):
                        eid = int(name.split('_')[1])
                        if eid > 0:
                            eid -= 1
                except Exception:
                    eid = 0
                ww.write_u8(eid & 255)
                L = int(round(float(getattr(s, 'L', 0.0)) * 255.0))
                R = int(round(float(getattr(s, 'R', 1.0)) * 255.0))
                ww.write_u8(L & 255); ww.write_u8(R & 255)
                last_t = t1
            b = ww.getvalue()
            w.write(encode_uvarint(len(b)))
            w.write(b)

        sx = _extract_track(getattr(ln, 'scale_x', None))
        sy = _extract_track(getattr(ln, 'scale_y', None))
        if sx is None:
            w.write_u8(0)
        else:
            b = _track_to_bytes(sx, q_scale)
            w.write_u8(1)
            w.write(encode_uvarint(len(b)))
            w.write(b)
        if sy is None:
            w.write_u8(0)
        else:
            b = _track_to_bytes(sy, q_scale)
            w.write_u8(1)
            w.write(encode_uvarint(len(b)))
            w.write(b)

        tt = _extract_text_track(getattr(ln, 'text', None))
        if tt is None:
            w.write_u8(0)
        else:
            w.write_u8(1)
            segs = list(getattr(tt, 'segs', []) or [])
            ww = ByteWriter()
            ww.write(encode_uvarint(len(segs)))
            last_t = 0
            for s in segs:
                t0 = sec_to_tick(float(s.t0))
                t1 = sec_to_tick(float(s.t1))
                ww.write(encode_uvarint(max(0, t0 - last_t)))
                ww.write(encode_uvarint(max(0, t1 - t0)))
                ww.write(encode_uvarint(_dict_id(s.s0, dict_pool, dict_list)))
                ww.write(encode_uvarint(_dict_id(s.s1, dict_pool, dict_list)))
                last_t = t1
            b = ww.getvalue()
            w.write(encode_uvarint(len(b)))
            w.write(b)

        gp = _extract_track(getattr(ln, 'gif_progress', None))
        if gp is None:
            w.write_u8(0)
        else:
            b = _track_to_bytes(gp, q_mul256)
            w.write_u8(1)
            w.write(encode_uvarint(len(b)))
            w.write(b)

    notes_sorted = sorted(list(notes), key=lambda n: float(n.t_hit))
    t_prev = 0
    for n in notes_sorted:
        t = sec_to_tick(float(n.t_hit))
        dt = max(0, t - t_prev)
        w.write(encode_uvarint(dt))
        t_prev = t

        w.write(encode_uvarint(int(n.line_id)))

        # kind uses 3 bits (1..4)
        kind = int(n.kind)
        if kind < 1 or kind > 4:
            kind = 1
        above = 1 if bool(n.above) else 0
        fake = 1 if bool(n.fake) else 0
        has_hitfx = 1 if (getattr(n, 'tint_hitfx_rgb', None) is not None) else 0
        has_hitsound = 1 if (getattr(n, 'hitsound_path', None) not in (None, '')) else 0
        flags = (kind & 0x7) | (above << 3) | (fake << 4) | (has_hitfx << 5) | (has_hitsound << 6)
        w.write_u8(flags)

        x_norm = float(n.x_local_px) / float(W)
        y_norm = float(n.y_offset_px) / float(H)
        w.write(encode_svarint(q_s14_norm(x_norm)))
        w.write(encode_svarint(q_s14_norm(y_norm)))

        if int(n.kind) == 3:
            dur = max(0, sec_to_tick(float(n.t_end)) - sec_to_tick(float(n.t_hit)))
            w.write(encode_uvarint(dur))

        w.write_u8(q_alpha01(float(n.alpha01)))

        w.write(encode_uvarint(q_mul256(float(n.speed_mul))))
        w.write(encode_uvarint(q_mul256(float(n.size_px))))

        tr = getattr(n, 'tint_rgb', (255, 255, 255))
        w.write_u8(int(tr[0]) & 255); w.write_u8(int(tr[1]) & 255); w.write_u8(int(tr[2]) & 255)

        if has_hitfx:
            hr = getattr(n, 'tint_hitfx_rgb', (255, 255, 255))
            w.write_u8(int(hr[0]) & 255); w.write_u8(int(hr[1]) & 255); w.write_u8(int(hr[2]) & 255)

        if has_hitsound:
            w.write(encode_uvarint(_dict_id(getattr(n, 'hitsound_path', ''), dict_pool, dict_list)))

    chart_bytes = w.getvalue()
    dict_bytes = encode_dict(dict_list)
    return meta_bytes, dict_bytes, chart_bytes


def decode_chart(
    meta_bytes: bytes,
    dict_bytes: bytes,
    chart_bytes: bytes,
    W: int,
    H: int,
) -> Tuple[float, Dict[str, object], List[RuntimeLine], List[RuntimeNote]]:
    dict_list = decode_dict(dict_bytes)
    meta = decode_meta(meta_bytes, dict_list)
    offset_sec = float(meta.get('offset_ms', 0)) / 1000.0

    r = ByteReader(chart_bytes)
    _flags, j = decode_uvarint(chart_bytes, r.pos)
    r.seek(j)
    line_count, j = decode_uvarint(chart_bytes, r.pos)
    r.seek(j)
    note_count, j = decode_uvarint(chart_bytes, r.pos)
    r.seek(j)

    lines: List[RuntimeLine] = []

    for lid in range(int(line_count)):
        name_id, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)
        father1, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)
        rot_with_f = bool(r.read_u8())
        tex_id, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)
        ax = dq_u16_01(r.read_u16le())
        ay = dq_u16_01(r.read_u16le())
        is_gif = bool(r.read_u8())

        name = dict_list[int(name_id) - 1] if int(name_id) > 0 and int(name_id) <= len(dict_list) else ''
        tex = dict_list[int(tex_id) - 1] if int(tex_id) > 0 and int(tex_id) <= len(dict_list) else None

        def _read_blob_len() -> bytes:
            ln, j = decode_uvarint(chart_bytes, r.pos)
            r.seek(j)
            return r.read(int(ln))

        bx = _read_blob_len()
        by = _read_blob_len()
        br = _read_blob_len()
        ba = _read_blob_len()
        bs = _read_blob_len()

        # decode tracks (normalized -> px)
        tx = _bytes_to_track(bx, dq_s14_norm)
        ty = _bytes_to_track(by, dq_s14_norm)
        trk = _bytes_to_track(br, dq_rot)
        ta = _bytes_to_track(ba, dq_alpha01)

        # scale normalized
        def px_track(track: PiecewiseEased, mul: float) -> PiecewiseEased:
            segs = [EasedSeg(s.t0, s.t1, s.v0 * mul, s.v1 * mul, s.easing, s.L, s.R) for s in track.segs]
            return PiecewiseEased(segs, default=track.default * mul)

        pos_x = px_track(tx, float(W))
        pos_y = px_track(ty, float(H))

        scroll_norm = _bytes_to_integral(bs, dq_s14_norm)
        segs = [Seg1D(s.t0, s.t1, s.v0 * float(H), s.v1 * float(H), s.prefix * float(H)) for s in scroll_norm.segs]
        scroll_px = IntegralTrack(segs)

        # optional color
        has_color = r.read_u8()
        color_track = None
        if has_color:
            ln, j = decode_uvarint(chart_bytes, r.pos)
            r.seek(j)
            blob = r.read(int(ln))
            rr = ByteReader(blob)
            n, j = decode_uvarint(blob, rr.pos)
            rr.seek(j)
            segs2: List[ColorSeg] = []
            tcur = 0
            for _ in range(int(n)):
                dt0, j = decode_uvarint(blob, rr.pos); rr.seek(j)
                dur, j = decode_uvarint(blob, rr.pos); rr.seek(j)
                c0 = (rr.read_u8(), rr.read_u8(), rr.read_u8())
                c1 = (rr.read_u8(), rr.read_u8(), rr.read_u8())
                eid = rr.read_u8()
                L = rr.read_u8(); R = rr.read_u8()
                t0 = tcur + int(dt0)
                t1 = t0 + int(dur)
                segs2.append(ColorSeg(tick_to_sec(t0), tick_to_sec(t1), c0, c1, easing_from_type(int(eid)), float(L)/255.0, float(R)/255.0))
                tcur = t1
            color_track = PiecewiseColor(segs2, default=(255, 255, 255))

        def _opt_track_q(dqfn):
            has = r.read_u8()
            if not has:
                return None
            ln, j = decode_uvarint(chart_bytes, r.pos)
            r.seek(j)
            blob = r.read(int(ln))
            return _bytes_to_track(blob, dqfn)

        scale_x = _opt_track_q(dq_scale)
        scale_y = _opt_track_q(dq_scale)

        # text
        has_text = r.read_u8()
        text_track = None
        if has_text:
            ln, j = decode_uvarint(chart_bytes, r.pos)
            r.seek(j)
            blob = r.read(int(ln))
            rr = ByteReader(blob)
            n, j = decode_uvarint(blob, rr.pos)
            rr.seek(j)
            segs3: List[TextSeg] = []
            tcur = 0
            for _ in range(int(n)):
                dt0, j = decode_uvarint(blob, rr.pos); rr.seek(j)
                dur, j = decode_uvarint(blob, rr.pos); rr.seek(j)
                s0, j = decode_uvarint(blob, rr.pos); rr.seek(j)
                s1, j = decode_uvarint(blob, rr.pos); rr.seek(j)
                t0 = tcur + int(dt0)
                t1 = t0 + int(dur)
                ss0 = dict_list[int(s0) - 1] if int(s0) > 0 and int(s0) <= len(dict_list) else ''
                ss1 = dict_list[int(s1) - 1] if int(s1) > 0 and int(s1) <= len(dict_list) else ''
                segs3.append(TextSeg(tick_to_sec(t0), tick_to_sec(t1), ss0, ss1))
                tcur = t1
            text_track = PiecewiseText(segs3, default='')

        gp = _opt_track_q(dq_mul256)

        ln = RuntimeLine(
            lid=int(lid),
            pos_x=pos_x,
            pos_y=pos_y,
            rot=trk,
            alpha=ta,
            scroll_px=scroll_px,
            color_rgb=(255, 255, 255),
            color=color_track,
            scale_x=scale_x,
            scale_y=scale_y,
            text=text_track,
            texture_path=tex,
            anchor=(ax, ay),
            is_gif=is_gif,
            gif_progress=gp,
            father=int(father1) - 1,
            rotate_with_father=bool(rot_with_f),
            name=name,
            event_counts={},
        )
        lines.append(ln)

    notes: List[RuntimeNote] = []
    tcur = 0
    for nid in range(int(note_count)):
        dt, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)
        tcur += int(dt)

        lid, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)
        flags = r.read_u8()
        kind = int(flags & 0x7) or 1
        above = bool((flags >> 3) & 1)
        fake = bool((flags >> 4) & 1)
        has_hitfx = bool((flags >> 5) & 1)
        has_hs = bool((flags >> 6) & 1)

        xq, j = decode_svarint(chart_bytes, r.pos)
        r.seek(j)
        yq, j = decode_svarint(chart_bytes, r.pos)
        r.seek(j)

        t_hit = tick_to_sec(tcur)
        t_end = t_hit
        if int(kind) == 3:
            dur, j = decode_uvarint(chart_bytes, r.pos)
            r.seek(j)
            t_end = tick_to_sec(tcur + int(dur))

        alpha01 = dq_alpha01(r.read_u8())

        sp, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)
        sz, j = decode_uvarint(chart_bytes, r.pos)
        r.seek(j)

        tint = (r.read_u8(), r.read_u8(), r.read_u8())
        tint_hitfx = None
        if has_hitfx:
            tint_hitfx = (r.read_u8(), r.read_u8(), r.read_u8())

        hs = None
        if has_hs:
            sid, j = decode_uvarint(chart_bytes, r.pos)
            r.seek(j)
            hs = dict_list[int(sid) - 1] if int(sid) > 0 and int(sid) <= len(dict_list) else None

        # normalized back to px
        x_local_px = dq_s14_norm(int(xq)) * float(W)
        y_offset_px = dq_s14_norm(int(yq)) * float(H)

        note = RuntimeNote(
            nid=int(nid),
            line_id=int(lid),
            kind=int(kind),
            above=bool(above),
            fake=bool(fake),
            t_hit=float(t_hit),
            t_end=float(t_end),
            x_local_px=float(x_local_px),
            y_offset_px=float(y_offset_px),
            speed_mul=dq_mul256(int(sp)),
            size_px=dq_mul256(int(sz)),
            alpha01=float(alpha01),
            tint_rgb=(int(tint[0]), int(tint[1]), int(tint[2])),
            tint_hitfx_rgb=tint_hitfx,
            hitsound_path=hs,
        )
        notes.append(note)

    # cache scroll samples
    line_map = {ln.lid: ln for ln in lines}
    for n in notes:
        ln = line_map.get(int(n.line_id))
        if ln is None:
            continue
        n.scroll_hit = ln.scroll_px.integral(float(n.t_hit))
        n.scroll_end = ln.scroll_px.integral(float(n.t_end))

    return offset_sec, meta, lines, notes
