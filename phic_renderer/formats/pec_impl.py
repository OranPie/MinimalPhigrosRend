from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ..math.easing import easing_from_type
from ..math.tracks import EasedSeg, PiecewiseEased, Seg1D, IntegralTrack
from ..math.util import clamp
from ..types import RuntimeLine, RuntimeNote


@dataclass
class _BpmSeg:
    beat0: float
    bpm: float
    sec_prefix: float


class _BpmMap:
    def __init__(self, segs: List[_BpmSeg]):
        self.segs = segs

    @staticmethod
    def build(items: List[Tuple[float, float]]) -> "_BpmMap":
        arr = [(float(b), float(v)) for (b, v) in items]
        arr.sort(key=lambda x: x[0])
        segs: List[_BpmSeg] = []
        sec_prefix = 0.0
        for i, (b0, bpm) in enumerate(arr):
            segs.append(_BpmSeg(b0, bpm, sec_prefix))
            if i + 1 < len(arr):
                b1 = arr[i + 1][0]
                sec_prefix += (b1 - b0) * 60.0 / max(1e-9, bpm)
        return _BpmMap(segs)

    def beat_to_sec(self, beat: float) -> float:
        if not self.segs:
            return 0.0
        segs = self.segs
        lo, hi = 0, len(segs)
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if segs[mid].beat0 <= beat:
                lo = mid
            else:
                hi = mid
        s = segs[lo]
        return s.sec_prefix + (beat - s.beat0) * 60.0 / max(1e-9, s.bpm)


def _pec_x_to_px(x: float, W: int) -> float:
    # PEC coordinate system (center-origin):
    # center = (0,0), left=-1024, right=1024.
    # Some packs store absolute coordinates in [0,2048]; auto-detect and shift.
    fx = float(x)
    if fx >= 1024.0 or fx <= -1024.0:
        fx = fx - 1024.0
    sx = float(W) / 2048.0
    return (fx + 1024.0) * sx


def _pec_y_to_px(y: float, H: int) -> float:
    # PEC coordinate system (center-origin):
    # center = (0,0), bottom=-700, top=700.
    # Some packs store absolute coordinates in [0,1400]; auto-detect and shift.
    fy = float(y)
    if fy >= 700.0 or fy <= -700.0:
        fy = fy - 700.0
    sy = float(H) / 1400.0
    return (float(H) * 0.5) - fy * sy


def load_pec_text(text: str, W: int, H: int) -> Tuple[float, List[RuntimeLine], List[RuntimeNote]]:
    raw_lines = [ln.strip() for ln in (text or "").splitlines()]
    raw_lines = [ln for ln in raw_lines if ln and (not ln.startswith("//"))]
    if not raw_lines:
        return 0.0, [], []

    try:
        offset_ms = int(raw_lines[0].strip())
    except:
        offset_ms = 0
    offset = float(offset_ms) / 1000.0

    bpm_items: List[Tuple[float, float]] = []
    for ln in raw_lines[1:]:
        if ln.startswith("bp "):
            parts = ln.split()
            if len(parts) >= 3:
                try:
                    bpm_items.append((float(parts[1]), float(parts[2])))
                except:
                    pass

    if not bpm_items:
        bpm_items = [(0.0, 120.0)]
    bpm_map = _BpmMap.build(bpm_items)

    max_line = -1
    notes_cmds: List[Tuple[str, List[str]]] = []
    ev_cmds: List[Tuple[str, List[str]]] = []

    for ln in raw_lines[1:]:
        if not ln:
            continue
        head = ln.split()[0]
        if head.startswith("n") or head in {"#", "&"}:
            notes_cmds.append((head, ln.split()[1:]))
            continue
        ev_cmds.append((head, ln.split()[1:]))

    for head, parts in ev_cmds:
        try:
            if head in {"cv", "cp", "cd", "ca", "cm", "cr", "cf"}:
                if parts:
                    max_line = max(max_line, int(parts[0]))
        except:
            pass
    for head, parts in notes_cmds:
        try:
            if head.startswith("n") and parts:
                max_line = max(max_line, int(parts[0]))
        except:
            pass

    line_count = max(0, max_line + 1)
    if line_count > 30:
        line_count = 30

    px_per_unit_per_sec = 120.0 * (float(H) / 900.0)

    def _build_tracks_for_line(lid: int) -> Tuple[Any, Any, Any, Any, Any]:
        cur_x, cur_y = 0.0, 0.0
        cur_rot = 0.0
        cur_alpha = 255.0
        cur_speed = 1.0

        x_segs: List[EasedSeg] = []
        y_segs: List[EasedSeg] = []
        r_segs: List[EasedSeg] = []
        a_segs: List[EasedSeg] = []
        speed_keys: List[Tuple[float, float]] = []

        t_cur = 0.0

        def emit_const(t0: float, t1: float):
            nonlocal cur_x, cur_y, cur_rot, cur_alpha
            if t1 <= t0 + 1e-9:
                return
            ease = easing_from_type(0)
            x_segs.append(EasedSeg(t0, t1, cur_x, cur_x, ease))
            y_segs.append(EasedSeg(t0, t1, cur_y, cur_y, ease))
            r_segs.append(EasedSeg(t0, t1, cur_rot, cur_rot, ease))
            a_segs.append(EasedSeg(t0, t1, cur_alpha, cur_alpha, ease))

        events: List[Tuple[float, str, List[str]]] = []
        for h, p in ev_cmds:
            if not p:
                continue
            try:
                if int(p[0]) != lid:
                    continue
            except:
                continue
            bt = None
            try:
                if h in {"cv", "cp", "cd", "ca"} and len(p) >= 2:
                    bt = float(p[1])
                elif h in {"cm", "cr", "cf"} and len(p) >= 2:
                    bt = float(p[1])
            except:
                bt = None
            if bt is None:
                continue
            events.append((bpm_map.beat_to_sec(bt), h, p))
        events.sort(key=lambda x: x[0])

        for t0, h, p in events:
            if t0 > t_cur:
                emit_const(t_cur, t0)
                t_cur = t0

            if h == "cp" and len(p) >= 4:
                try:
                    cur_x = float(p[2])
                    cur_y = float(p[3])
                except:
                    pass
                continue

            if h == "cd" and len(p) >= 3:
                try:
                    cur_rot = float(p[2])
                except:
                    pass
                continue

            if h == "ca" and len(p) >= 3:
                try:
                    v = float(p[2])
                    if v < 0:
                        v = 0.0
                    cur_alpha = clamp(v, 0.0, 255.0)
                except:
                    pass
                continue

            if h == "cv" and len(p) >= 3:
                try:
                    cur_speed = float(p[2])
                except:
                    pass
                speed_keys.append((t0, cur_speed))
                continue

            if h == "cm" and len(p) >= 6:
                try:
                    t1 = bpm_map.beat_to_sec(float(p[2]))
                    x1 = float(p[3])
                    y1 = float(p[4])
                    et = int(p[5])
                except:
                    continue
                if t1 > t0 + 1e-9:
                    ease = easing_from_type(et)
                    x_segs.append(EasedSeg(t0, t1, cur_x, x1, ease))
                    y_segs.append(EasedSeg(t0, t1, cur_y, y1, ease))
                    r_segs.append(EasedSeg(t0, t1, cur_rot, cur_rot, easing_from_type(0)))
                    a_segs.append(EasedSeg(t0, t1, cur_alpha, cur_alpha, easing_from_type(0)))
                    cur_x, cur_y = x1, y1
                    t_cur = t1
                continue

            if h == "cr" and len(p) >= 5:
                try:
                    t1 = bpm_map.beat_to_sec(float(p[2]))
                    r1 = float(p[3])
                    et = int(p[4])
                except:
                    continue
                if t1 > t0 + 1e-9:
                    ease = easing_from_type(et)
                    r_segs.append(EasedSeg(t0, t1, cur_rot, r1, ease))
                    x_segs.append(EasedSeg(t0, t1, cur_x, cur_x, easing_from_type(0)))
                    y_segs.append(EasedSeg(t0, t1, cur_y, cur_y, easing_from_type(0)))
                    a_segs.append(EasedSeg(t0, t1, cur_alpha, cur_alpha, easing_from_type(0)))
                    cur_rot = r1
                    t_cur = t1
                continue

            if h == "cf" and len(p) >= 4:
                try:
                    t1 = bpm_map.beat_to_sec(float(p[2]))
                    a1 = float(p[3])
                    et = int(p[4]) if len(p) >= 5 else 0
                except:
                    continue
                if a1 < 0:
                    a1 = 0.0
                a1 = clamp(a1, 0.0, 255.0)
                if t1 > t0 + 1e-9:
                    ease = easing_from_type(et)
                    a_segs.append(EasedSeg(t0, t1, cur_alpha, a1, ease))
                    x_segs.append(EasedSeg(t0, t1, cur_x, cur_x, easing_from_type(0)))
                    y_segs.append(EasedSeg(t0, t1, cur_y, cur_y, easing_from_type(0)))
                    r_segs.append(EasedSeg(t0, t1, cur_rot, cur_rot, easing_from_type(0)))
                    cur_alpha = a1
                    t_cur = t1
                continue

        end_hint = 0.0
        for head, parts in notes_cmds:
            if head.startswith("n") and parts:
                try:
                    if int(parts[0]) != lid:
                        continue
                except:
                    continue
                try:
                    if head == "n2" and len(parts) >= 2:
                        end_hint = max(end_hint, bpm_map.beat_to_sec(float(parts[2])))
                    elif len(parts) >= 2:
                        end_hint = max(end_hint, bpm_map.beat_to_sec(float(parts[1])))
                except:
                    pass
        end_time = max(end_hint + 5.0, t_cur + 2.0)
        emit_const(t_cur, end_time)

        px = PiecewiseEased(x_segs, default=0.0)
        py = PiecewiseEased(y_segs, default=0.0)
        pr = PiecewiseEased(r_segs, default=0.0)
        pa = PiecewiseEased(a_segs, default=255.0)

        if not speed_keys:
            speed_keys = [(0.0, cur_speed)]
        speed_keys.sort(key=lambda x: x[0])
        cuts = [t for (t, _v) in speed_keys]
        cuts = sorted(set([0.0] + cuts + [end_time]))
        segs: List[Seg1D] = []
        prefix = 0.0
        for i in range(len(cuts) - 1):
            t0, t1 = cuts[i], cuts[i + 1]
            if t1 <= t0:
                continue
            v = speed_keys[0][1]
            for tt, vv in speed_keys:
                if tt <= t0 + 1e-9:
                    v = vv
                else:
                    break
            vpx = float(v) * px_per_unit_per_sec
            segs.append(Seg1D(t0, t1, vpx, vpx, prefix))
            prefix += vpx * (t1 - t0)
        scroll = IntegralTrack(segs)
        return px, py, pr, pa, scroll

    tracks_by_line = [_build_tracks_for_line(i) for i in range(line_count)]

    lines_out: List[RuntimeLine] = []
    for lid in range(line_count):
        px, py, pr, pa, scroll = tracks_by_line[lid]
        pos_x = lambda t, px=px: _pec_x_to_px(px.eval(t), W)
        pos_y = lambda t, py=py: _pec_y_to_px(py.eval(t), H)
        rot = lambda t, pr=pr: float(pr.eval(t)) * 3.141592653589793 / 180.0

        def alpha01(t, pa=pa):
            v = float(pa.eval(t))
            if v <= 1.000001:
                return clamp(v, 0.0, 1.0)
            return clamp(v / 255.0, 0.0, 1.0)

        lines_out.append(RuntimeLine(lid=lid, pos_x=pos_x, pos_y=pos_y, rot=rot, alpha=alpha01, scroll_px=scroll, color_rgb=(255, 255, 255)))

    notes_out: List[RuntimeNote] = []
    nid = 0
    pending_note: Optional[Dict[str, Any]] = None

    for head, parts in notes_cmds:
        if head.startswith("n"):
            pending_note = None
            if not parts:
                continue
            tp = int(head[1:])
            if tp not in {1, 2, 3, 4}:
                continue
            try:
                lid = int(parts[0])
            except:
                continue
            if lid < 0 or lid >= line_count:
                continue

            try:
                if tp == 2:
                    b0 = float(parts[1])
                    b1 = float(parts[2])
                    x = float(parts[3])
                    direction = int(parts[4])
                    fake = int(parts[5]) == 1
                else:
                    b0 = float(parts[1])
                    b1 = b0
                    x = float(parts[2])
                    direction = int(parts[3])
                    fake = int(parts[4]) == 1
            except:
                continue

            t_hit = bpm_map.beat_to_sec(b0)
            t_end = bpm_map.beat_to_sec(b1)
            above = True if direction == 1 else False

            pending_note = {
                "line_id": lid,
                "kind": 3 if tp == 2 else tp,
                "t_hit": t_hit,
                "t_end": t_end if tp == 2 else t_hit,
                "x_local_px": x * (float(W) / 2048.0),
                "above": above,
                "fake": bool(fake),
                "speed_mul": 1.0,
                "size_px": 1.0,
            }
            continue

        if head == "#" and pending_note is not None:
            try:
                if parts:
                    pending_note["speed_mul"] = float(parts[0])
            except:
                pass
            continue

        if head == "&" and pending_note is not None:
            try:
                if parts:
                    pending_note["size_px"] = float(parts[0])
            except:
                pass

            note = RuntimeNote(
                nid=nid,
                line_id=int(pending_note["line_id"]),
                kind=int(pending_note["kind"]),
                above=bool(pending_note["above"]),
                fake=bool(pending_note["fake"]),
                t_hit=float(pending_note["t_hit"]),
                t_end=float(pending_note["t_end"]),
                x_local_px=float(pending_note["x_local_px"]),
                y_offset_px=0.0,
                speed_mul=float(pending_note["speed_mul"]),
                size_px=float(pending_note["size_px"]),
                alpha01=1.0,
            )
            notes_out.append(note)
            nid += 1
            pending_note = None

    return offset, lines_out, sorted(notes_out, key=lambda x: x.t_hit)


def load_pec(path: str, W: int, H: int) -> Tuple[float, List[RuntimeLine], List[RuntimeNote]]:
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    return load_pec_text(text, W, H)
