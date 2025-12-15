from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any, Dict, List, Tuple

from .util import clamp, lerp, hsv_to_rgb
from .tracks import EasedSeg, PiecewiseEased, SumTrack, Seg1D, IntegralTrack
from .types import RuntimeLine, RuntimeNote
from . import easing  # keep module ref to access easing.rpe_easing_shift
from .easing import easing_from_type, cubic_bezier_y_for_x

def beat_to_value(b: Any) -> float:
    # Beat can be [a,b,c] or {"a":..} etc; support common cases
    if isinstance(b, list) and len(b) == 3:
        a, n, d = b
        return float(a) + float(n) / float(d)
    if isinstance(b, tuple) and len(b) == 3:
        a, n, d = b
        return float(a) + float(n) / float(d)
    # Some exports might store beat as dict
    if isinstance(b, dict) and {"bar","num","den"} <= set(b.keys()):
        return float(b["bar"]) + float(b["num"]) / float(b["den"])
    # Fallback: assume numeric
    return float(b)

@dataclass
class BpmSeg:
    beat0: float
    bpm: float
    sec_prefix: float

class BpmMap:
    def __init__(self, segs: List[BpmSeg]):
        self.segs = segs

    @staticmethod
    def build(bpm_list: List[Dict[str, Any]]) -> "BpmMap":
        items = []
        for e in bpm_list:
            b0 = beat_to_value(e["startTime"])
            bpm = float(e["bpm"])
            items.append((b0, bpm))
        items.sort(key=lambda x: x[0])

        segs: List[BpmSeg] = []
        sec_prefix = 0.0
        for i, (b0, bpm) in enumerate(items):
            segs.append(BpmSeg(b0, bpm, sec_prefix))
            if i + 1 < len(items):
                b1 = items[i+1][0]
                sec_prefix += (b1 - b0) * 60.0 / bpm
        return BpmMap(segs)

    def beat_to_sec(self, beat_val: float, bpmfactor: float = 1.0) -> float:
        # effective bpm = bpm / bpmfactor => sec per beat multiply by bpmfactor
        if not self.segs:
            return 0.0
        segs = self.segs
        # find last seg with beat0 <= beat_val
        lo, hi = 0, len(segs)
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if segs[mid].beat0 <= beat_val:
                lo = mid
            else:
                hi = mid
        s = segs[lo]
        return (s.sec_prefix + (beat_val - s.beat0) * 60.0 / s.bpm) * bpmfactor

def build_rpe_eased_track(events: List[Dict[str, Any]],
                          bpm_map: BpmMap,
                          bpmfactor: float,
                          default: float = 0.0) -> PiecewiseEased:
    evs = list(events or [])
    if not evs:
        return PiecewiseEased([], default=default)
    evs.sort(key=lambda e: beat_to_value(e["startTime"]))

    segs: List[EasedSeg] = []
    for e in evs:
        b0 = beat_to_value(e["startTime"])
        b1 = beat_to_value(e["endTime"])
        t0 = bpm_map.beat_to_sec(b0, bpmfactor)
        t1 = bpm_map.beat_to_sec(b1, bpmfactor)
        v0 = float(e.get("start", 0.0))
        v1 = float(e.get("end", 0.0))

        L = float(e.get("easingLeft", 0.0) or 0.0)
        R = float(e.get("easingRight", 1.0) or 1.0)

        bez = int(e.get("bezier", 0) or 0)
        if bez == 1 and isinstance(e.get("bezierPoints"), list) and len(e["bezierPoints"]) == 4:
            x1, y1, x2, y2 = map(float, e["bezierPoints"])
            easing = lambda p, x1=x1,y1=y1,x2=x2,y2=y2: cubic_bezier_y_for_x(x1,y1,x2,y2,p)
        else:
            tp = int(e.get("easingType", 0) or 0)
            tp = tp + int(rpe_easing_shift)
            easing = easing_from_type(tp)

        segs.append(EasedSeg(t0, t1, v0, v1, easing, L=L, R=R))

    if segs and segs[0].t0 > 0:
        segs.insert(0, EasedSeg(0.0, segs[0].t0, segs[0].v0, segs[0].v0, ease_01))
    return PiecewiseEased(segs, default=default)

def build_rpe_scroll_px(speed_events_layers: List[List[Dict[str, Any]]],
                        bpm_map: BpmMap,
                        bpmfactor: float,
                        px_per_unit_per_sec: float) -> IntegralTrack:
    """
    Minimal: layer values are summed; speed event interpolated linearly between start/end.
    Convert to px/s via px_per_unit_per_sec multiplier.
    """
    # collect all events
    all_evs = []
    for layer in speed_events_layers:
        for e in (layer or []):
            all_evs.append(e)
    if not all_evs:
        return IntegralTrack([])

    # Build segments by cutting at all boundaries (seconds)
    cuts = set([0.0])
    for e in all_evs:
        b0 = beat_to_value(e["startTime"])
        b1 = beat_to_value(e["endTime"])
        cuts.add(bpm_map.beat_to_sec(b0, bpmfactor))
        cuts.add(bpm_map.beat_to_sec(b1, bpmfactor))
    cut_list = sorted(cuts)

    def sample_layer_value(layer_events, t_mid):
        # find first covering event; if multiple, sum them too (simple)
        val = 0.0
        for e in (layer_events or []):
            t0 = bpm_map.beat_to_sec(beat_to_value(e["startTime"]), bpmfactor)
            t1 = bpm_map.beat_to_sec(beat_to_value(e["endTime"]), bpmfactor)
            if t_mid >= t0 and t_mid < t1:
                # linear between start/end
                s0 = float(e.get("start", 0.0))
                s1 = float(e.get("end", s0))
                u = (t_mid - t0) / max(1e-9, (t1 - t0))
                val += lerp(s0, s1, clamp(u, 0, 1))
        return val

    segs: List[Seg1D] = []
    prefix = 0.0
    for i in range(len(cut_list) - 1):
        t0, t1 = cut_list[i], cut_list[i+1]
        if t1 <= t0:
            continue
        tm = (t0 + t1) * 0.5
        v_unit = 0.0
        for layer in speed_events_layers:
            v_unit += sample_layer_value(layer, tm)
        v = v_unit * px_per_unit_per_sec
        segs.append(Seg1D(t0, t1, v, v, prefix))
        prefix += v * (t1 - t0)

    return IntegralTrack(segs)

def load_rpe(data: Dict[str, Any], W: int, H: int) -> Tuple[float, List[RuntimeLine], List[RuntimeNote]]:
    meta = data.get("META", {})
    offset_ms = float(meta.get("offset", 0.0))
    offset = offset_ms / 1000.0

    bpm_map = BpmMap.build(data.get("BPMList", []))
    jls = data.get("judgeLineList", [])

    sx = W / 1350.0
    sy = H / 900.0

    # speed unit -> px/s scaling (minimal convention)
    # 1.0 speed ≈ 120 px/s in "base 900p"; scale by sy with resolution.
    px_per_unit_per_sec = 120.0 * sy

    lines_out: List[RuntimeLine] = []
    notes_out: List[RuntimeNote] = []

    for i, jl in enumerate(jls):
        bpmfactor = float(jl.get("bpmfactor", 1.0) or 1.0)

        layers = jl.get("eventLayers", []) or []
        # each layer can be null
        move_x_tracks = []
        move_y_tracks = []
        rot_tracks = []
        alpha_tracks = []
        speed_layers = []

        for layer in layers:
            if layer is None:
                continue
            move_x_tracks.append(build_rpe_eased_track(layer.get("moveXEvents", []), bpm_map, bpmfactor, default=0.0))
            move_y_tracks.append(build_rpe_eased_track(layer.get("moveYEvents", []), bpm_map, bpmfactor, default=0.0))
            rot_tracks.append(build_rpe_eased_track(layer.get("rotateEvents", []), bpm_map, bpmfactor, default=0.0))
            alpha_tracks.append(build_rpe_eased_track(layer.get("alphaEvents", []), bpm_map, bpmfactor, default=255.0))
            speed_layers.append(layer.get("speedEvents", []) or [])

        evc = {
            "moveX": sum(len((ly or {}).get("moveXEvents", []) or []) for ly in (layers or []) if ly),
            "moveY": sum(len((ly or {}).get("moveYEvents", []) or []) for ly in (layers or []) if ly),
            "rot": sum(len((ly or {}).get("rotateEvents", []) or []) for ly in (layers or []) if ly),
            "alpha": sum(len((ly or {}).get("alphaEvents", []) or []) for ly in (layers or []) if ly),
            "speed": sum(len((ly or {}).get("speedEvents", []) or []) for ly in (layers or []) if ly),
        }
        name = str(jl.get("name", "") or "")

        move_x = SumTrack(move_x_tracks, default=0.0)
        move_y = SumTrack(move_y_tracks, default=0.0)
        rot_deg = SumTrack(rot_tracks, default=0.0)
        alpha_raw = SumTrack(alpha_tracks, default=255.0)

        # convert RPE coords -> pixel center using conrpepos
        # x_px = (x+675)/1350 * W  == (x+675)*sx
        # y_px = 1-(y+450)/900 * H == (450 - y)*sy
        pos_x = lambda t, mx=move_x: (mx.eval(t) + 675.0) * sx
        pos_y = lambda t, my=move_y: (450.0 - my.eval(t)) * sy
        rot = lambda t, rd=rot_deg: (rd.eval(t) * math.pi / 180.0)

        def alpha01(t, a=alpha_raw):
            # sum layers, raw 0..255 typically; normalize
            v = float(a.eval(t))
            if v <= 1.000001:
                return clamp(v, 0.0, 1.0)
            return clamp(v / 255.0, 0.0, 1.0)

        scroll = build_rpe_scroll_px(speed_layers, bpm_map, bpmfactor, px_per_unit_per_sec)

        rgb = hsv_to_rgb((i / max(1, len(jls))) % 1.0, 0.65, 0.95)
        lines_out.append(RuntimeLine(
            lid=i,
            pos_x=pos_x, pos_y=pos_y,
            rot=rot,
            alpha=alpha01,
            scroll_px=scroll,
            color_rgb=rgb,
            name=name,
            event_counts=evc
        ))

        # notes
        nid_base = i * 100000
        nid = nid_base
        for n in (jl.get("notes", []) or []):
            kind = int(n.get("type", 1))
            b0 = beat_to_value(n.get("startTime", [0,0,1]))
            b1 = beat_to_value(n.get("endTime", n.get("startTime", [0,0,1])))
            t_hit = bpm_map.beat_to_sec(b0, bpmfactor)
            t_end = bpm_map.beat_to_sec(b1, bpmfactor)

            above = int(n.get("above", 1)) == 1
            fake = int(n.get("isFake", 0)) == 1

            posx_units = float(n.get("positionX", 0.0))
            y_offset_units = float(n.get("yOffset", 0.0))
            size = float(n.get("size", 1.0))
            speed_mul = float(n.get("speed", 1.0))

            na = n.get("alpha", None)
            alpha_note = 1.0 if na is None else clamp(float(na) / 255.0, 0.0, 1.0)

            hs = n.get("hitsound", None)  # relative to chart root directory

            note = RuntimeNote(
                nid=nid,
                line_id=i,
                kind=kind,
                above=above,
                fake=fake,
                t_hit=t_hit,
                t_end=t_end if kind == 3 else t_hit,  # only hold uses end
                x_local_px=posx_units * sx,           # local units -> px
                y_offset_px=y_offset_units * sy,
                speed_mul=speed_mul,
                size_px=size,
                alpha01=alpha_note,
                hitsound_path=hs
            )
            notes_out.append(note)
            nid += 1

    # cache scroll samples
    line_map = {ln.lid: ln for ln in lines_out}
    for n in notes_out:
        ln = line_map[n.line_id]
        n.scroll_hit = ln.scroll_px.integral(n.t_hit)
        n.scroll_end = ln.scroll_px.integral(n.t_end)

    return offset, lines_out, sorted(notes_out, key=lambda x: x.t_hit)
