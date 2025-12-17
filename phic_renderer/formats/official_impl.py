from __future__ import annotations

import math
from typing import Any, Dict, List, Tuple

from ..math.util import hsv_to_rgb
from ..math.easing import ease_01
from ..math.tracks import EasedSeg, PiecewiseEased, Seg1D, IntegralTrack
from ..types import RuntimeLine, RuntimeNote


def official_unit_sec(bpm: float) -> float:
    return 1.875 / bpm


def u_to_sec(u: float, bpm: float) -> float:
    return u * official_unit_sec(bpm)


def build_official_scroll_px(speed_events, bpm: float, Uh_px: float) -> IntegralTrack:
    """
    For AT.json, validated:
      dF/dt = speed.value   where F is floorPosition in height-units.
    So pixel scroll S(t) = F(t) * Uh_px.
    """
    evs = list(speed_events)
    evs.sort(key=lambda e: float(e.get("startTime", 0)))
    segs: List[Seg1D] = []
    prefix = 0.0

    if not evs:
        return IntegralTrack([])

    for e in evs:
        t0 = u_to_sec(float(e["startTime"]), bpm)
        t1 = u_to_sec(float(e["endTime"]), bpm)
        v = float(e["value"]) * Uh_px  # (heightUnits/sec) * px/heightUnit => px/sec
        seg = Seg1D(t0=t0, t1=t1, v0=v, v1=v, prefix=prefix)
        dt = max(0.0, t1 - t0)
        prefix += 0.5 * (seg.v0 + seg.v1) * dt
        segs.append(seg)

    if segs and segs[0].t0 > 0:
        v0 = segs[0].v0
        segs.insert(0, Seg1D(0.0, segs[0].t0, v0, v0, 0.0))
        # rebuild prefix
        prefix = 0.0
        for i, s in enumerate(segs):
            segs[i] = Seg1D(s.t0, s.t1, s.v0, s.v1, prefix)
            dt = max(0.0, s.t1 - s.t0)
            prefix += 0.5 * (s.v0 + s.v1) * dt

    return IntegralTrack(segs)


def build_official_pos_tracks(move_events, bpm: float, fmt: int, W: int, H: int) -> Tuple[PiecewiseEased, PiecewiseEased]:
    evs = list(move_events)
    evs.sort(key=lambda e: float(e.get("startTime", 0)))
    if not evs:
        # default center
        return PiecewiseEased([], default=W * 0.5), PiecewiseEased([], default=H * 0.5)
    if fmt != 3:
        raise ValueError(f"Minimal renderer supports official formatVersion=3 only (got {fmt}).")

    sx: List[EasedSeg] = []
    sy: List[EasedSeg] = []
    for e in evs:
        t0 = u_to_sec(float(e["startTime"]), bpm)
        t1 = u_to_sec(float(e["endTime"]), bpm)
        x0 = float(e["start"]) * W
        x1 = float(e["end"]) * W
        # Official format: bottom-left is (0,0), +Y is upward; pygame: top-left is (0,0), +Y is downward
        # So we need to flip: y_pygame = H - y_official * H = H * (1 - y_official)
        y0 = H * (1.0 - float(e["start2"]))
        y1 = H * (1.0 - float(e["end2"]))
        sx.append(EasedSeg(t0, t1, x0, x1, ease_01))
        sy.append(EasedSeg(t0, t1, y0, y1, ease_01))

    # extend to t=0
    if sx[0].t0 > 0:
        sx.insert(0, EasedSeg(0.0, sx[0].t0, sx[0].v0, sx[0].v0, ease_01))
        sy.insert(0, EasedSeg(0.0, sy[0].t0, sy[0].v0, sy[0].v0, ease_01))
    return PiecewiseEased(sx, default=W * 0.5), PiecewiseEased(sy, default=H * 0.5)


def build_official_rot_track(rot_events, bpm: float) -> PiecewiseEased:
    evs = list(rot_events)
    evs.sort(key=lambda e: float(e.get("startTime", 0)))
    segs: List[EasedSeg] = []
    if not evs:
        return PiecewiseEased([], default=0.0)
    for e in evs:
        t0 = u_to_sec(float(e["startTime"]), bpm)
        t1 = u_to_sec(float(e["endTime"]), bpm)
        a0 = -float(e["start"]) * math.pi / 180.0
        a1 = -float(e["end"]) * math.pi / 180.0
        segs.append(EasedSeg(t0, t1, a0, a1, ease_01))
    if segs[0].t0 > 0:
        segs.insert(0, EasedSeg(0.0, segs[0].t0, segs[0].v0, segs[0].v0, ease_01))
    return PiecewiseEased(segs, default=0.0)


def build_official_alpha_track(disp_events, bpm: float) -> PiecewiseEased:
    evs = list(disp_events)
    evs.sort(key=lambda e: float(e.get("startTime", 0)))
    segs: List[EasedSeg] = []
    if not evs:
        return PiecewiseEased([], default=1.0)
    for e in evs:
        t0 = u_to_sec(float(e["startTime"]), bpm)
        t1 = u_to_sec(float(e["endTime"]), bpm)
        a0 = float(e["start"])
        a1 = float(e["end"])
        segs.append(EasedSeg(t0, t1, a0, a1, ease_01))
    if segs[0].t0 > 0:
        segs.insert(0, EasedSeg(0.0, segs[0].t0, segs[0].v0, segs[0].v0, ease_01))
    return PiecewiseEased(segs, default=1.0)


def load_official(data: Dict[str, Any], W: int, H: int) -> Tuple[float, List[RuntimeLine], List[RuntimeNote]]:
    fmt = int(data.get("formatVersion", 3))
    offset = float(data.get("offset", 0.0))  # seconds

    Uw = 0.05625 * W
    Uh = 0.6 * H

    lines_out: List[RuntimeLine] = []
    notes_out: List[RuntimeNote] = []

    jls = data["judgeLineList"]
    for i, jl in enumerate(jls):
        bpm = float(jl.get("bpm", 120.0))
        px, py = build_official_pos_tracks(jl.get("judgeLineMoveEvents", []), bpm, fmt, W, H)
        rot = build_official_rot_track(jl.get("judgeLineRotateEvents", []), bpm)
        alpha = build_official_alpha_track(jl.get("judgeLineDisappearEvents", []), bpm)
        scroll = build_official_scroll_px(jl.get("speedEvents", []), bpm, Uh)

        # color per line
        rgb = hsv_to_rgb((i / max(1, len(jls))) % 1.0, 0.65, 0.95)

        name = str(jl.get("name", "") or "")
        evc = {
            "move": len(jl.get("judgeLineMoveEvents", []) or []),
            "rot": len(jl.get("judgeLineRotateEvents", []) or []),
            "alpha": len(jl.get("judgeLineDisappearEvents", []) or []),
            "speed": len(jl.get("speedEvents", []) or []),
        }

        lines_out.append(
            RuntimeLine(lid=i, pos_x=px, pos_y=py, rot=rot, alpha=alpha, scroll_px=scroll, color_rgb=rgb, name=name, event_counts=evc)
        )

        # notes
        nid_base = i * 100000
        nid = nid_base

        def add_note(n: Dict[str, Any], above: bool):
            nonlocal nid
            kind = int(n["type"])
            t_hit = u_to_sec(float(n["time"]), bpm)
            hold_u = float(n.get("holdTime", 0.0))
            t_end = t_hit + u_to_sec(hold_u, bpm) if kind == 3 and hold_u > 0 else t_hit

            note = RuntimeNote(
                nid=nid,
                line_id=i,
                kind=kind,
                above=above,
                fake=False,
                t_hit=t_hit,
                t_end=t_end,
                x_local_px=float(n.get("positionX", 0.0)) * Uw,
                y_offset_px=0.0,
                speed_mul=float(n.get("speed", 1.0)),
                size_px=1.0,
                alpha01=1.0,
            )
            notes_out.append(note)
            nid += 1

        for n in jl.get("notesAbove", []):
            # Y-axis is flipped for official format, so above/below semantics are reversed
            add_note(n, False)
        for n in jl.get("notesBelow", []):
            # Y-axis is flipped for official format, so above/below semantics are reversed
            add_note(n, True)

    # cache scroll samples
    line_map = {ln.lid: ln for ln in lines_out}
    for n in notes_out:
        ln = line_map[n.line_id]
        n.scroll_hit = ln.scroll_px.integral(n.t_hit)
        if int(n.kind) == 3 and float(n.t_end) > float(n.t_hit):
            try:
                dur = max(0.0, float(n.t_end) - float(n.t_hit))
                sp = max(0.0, float(n.speed_mul))
                n.scroll_end = float(n.scroll_hit) + sp * dur * float(Uh)
                n.speed_mul = 1.0
            except:
                n.scroll_end = ln.scroll_px.integral(n.t_end)
        else:
            n.scroll_end = ln.scroll_px.integral(n.t_end)

    return offset, lines_out, sorted(notes_out, key=lambda x: x.t_hit)
