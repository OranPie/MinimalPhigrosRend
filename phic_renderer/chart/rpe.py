from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ..math.util import clamp, lerp, hsv_to_rgb
from ..math.tracks import (
    ColorSeg,
    EasedSeg,
    IntegralTrack,
    PiecewiseColor,
    PiecewiseEased,
    PiecewiseText,
    Seg1D,
    SumTrack,
    TextSeg,
)
from ..types import RuntimeLine, RuntimeNote
from ..math import easing  # keep module ref to access easing.rpe_easing_shift
from ..math.easing import easing_from_type, cubic_bezier_y_for_x


def beat_to_value(b: Any) -> float:
    # Beat can be [a,b,c] or {"a":..} etc; support common cases
    if isinstance(b, list) and len(b) == 3:
        a, n, d = b
        return float(a) + float(n) / float(d)
    if isinstance(b, tuple) and len(b) == 3:
        a, n, d = b
        return float(a) + float(n) / float(d)
    # Some exports might store beat as dict
    if isinstance(b, dict) and {"bar", "num", "den"} <= set(b.keys()):
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
                b1 = items[i + 1][0]
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


def build_rpe_eased_track(
    events: List[Dict[str, Any]],
    bpm_map: BpmMap,
    bpmfactor: float,
    default: float = 0.0,
) -> PiecewiseEased:
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
            easing_f = lambda p, x1=x1, y1=y1, x2=x2, y2=y2: cubic_bezier_y_for_x(x1, y1, x2, y2, p)
        else:
            tp = int(e.get("easingType", 0) or 0)
            tp = tp + int(easing.rpe_easing_shift)
            easing_f = easing_from_type(tp)

        segs.append(EasedSeg(t0, t1, v0, v1, easing_f, L=L, R=R))

    if segs and segs[0].t0 > 0:
        from ..math.easing import ease_01

        segs.insert(0, EasedSeg(0.0, segs[0].t0, segs[0].v0, segs[0].v0, ease_01))
    return PiecewiseEased(segs, default=default)


def _parse_rgb3(v: Any) -> Tuple[int, int, int]:
    if isinstance(v, (list, tuple)) and len(v) >= 3:
        try:
            r = int(v[0])
            g = int(v[1])
            b = int(v[2])
            return (clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255))
        except:
            return (255, 255, 255)
    return (255, 255, 255)


def _parse_rgb3_opt(v: Any) -> Optional[Tuple[int, int, int]]:
    if v is None:
        return None
    try:
        return _parse_rgb3(v)
    except:
        return None


def build_rpe_color_track(
    events: List[Dict[str, Any]],
    bpm_map: BpmMap,
    bpmfactor: float,
    default: Tuple[int, int, int],
) -> PiecewiseColor:
    evs = list(events or [])
    if not evs:
        return PiecewiseColor([], default=default)
    evs.sort(key=lambda e: beat_to_value(e["startTime"]))

    segs: List[ColorSeg] = []
    for e in evs:
        b0 = beat_to_value(e["startTime"])
        b1 = beat_to_value(e["endTime"])
        t0 = bpm_map.beat_to_sec(b0, bpmfactor)
        t1 = bpm_map.beat_to_sec(b1, bpmfactor)
        c0 = _parse_rgb3(e.get("start", default))
        c1 = _parse_rgb3(e.get("end", c0))

        L = float(e.get("easingLeft", 0.0) or 0.0)
        R = float(e.get("easingRight", 1.0) or 1.0)

        bez = int(e.get("bezier", 0) or 0)
        if bez == 1 and isinstance(e.get("bezierPoints"), list) and len(e["bezierPoints"]) == 4:
            x1, y1, x2, y2 = map(float, e["bezierPoints"])
            easing_f = lambda p, x1=x1, y1=y1, x2=x2, y2=y2: cubic_bezier_y_for_x(x1, y1, x2, y2, p)
        else:
            tp = int(e.get("easingType", 0) or 0)
            tp = tp + int(easing.rpe_easing_shift)
            easing_f = easing_from_type(tp)

        segs.append(ColorSeg(t0, t1, c0, c1, easing_f, L=L, R=R))

    return PiecewiseColor(segs, default=default)


def build_rpe_text_track(
    events: List[Dict[str, Any]],
    bpm_map: BpmMap,
    bpmfactor: float,
) -> PiecewiseText:
    evs = list(events or [])
    if not evs:
        return PiecewiseText([], default="")
    evs.sort(key=lambda e: beat_to_value(e["startTime"]))

    segs: List[TextSeg] = []
    for e in evs:
        b0 = beat_to_value(e["startTime"])
        b1 = beat_to_value(e["endTime"])
        t0 = bpm_map.beat_to_sec(b0, bpmfactor)
        t1 = bpm_map.beat_to_sec(b1, bpmfactor)
        s0 = str(e.get("start", "") or "")
        s1 = str(e.get("end", s0) or "")
        segs.append(TextSeg(t0, t1, s0, s1))

    return PiecewiseText(segs, default="")


def build_rpe_scroll_px(
    speed_events_layers: List[List[Dict[str, Any]]],
    bpm_map: BpmMap,
    bpmfactor: float,
    px_per_unit_per_sec: float,
) -> IntegralTrack:
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
    cuts = {0.0}
    for e in all_evs:
        b0 = beat_to_value(e["startTime"])
        b1 = beat_to_value(e["endTime"])
        cuts.add(bpm_map.beat_to_sec(b0, bpmfactor))
        cuts.add(bpm_map.beat_to_sec(b1, bpmfactor))
    cut_list = sorted(cuts)
    if not cut_list:
        cut_list = [0.0]
    if len(cut_list) == 1:
        cut_list.append(cut_list[0] + 1e6)
    else:
        cut_list.append(cut_list[-1] + 1e6)

    def sample_layer_value(layer_events, t_mid):
        evs = list(layer_events or [])
        if not evs:
            return 0.0
        # sort by start time so we can extend the last value across gaps
        evs.sort(key=lambda e: bpm_map.beat_to_sec(beat_to_value(e["startTime"]), bpmfactor))

        val = 0.0
        any_cover = False
        last_before = None
        for e in evs:
            t0 = bpm_map.beat_to_sec(beat_to_value(e["startTime"]), bpmfactor)
            t1 = bpm_map.beat_to_sec(beat_to_value(e["endTime"]), bpmfactor)
            if t_mid < t0:
                break
            last_before = e
            if t_mid >= t0 and t_mid < t1:
                s0 = float(e.get("start", 0.0))
                s1 = float(e.get("end", s0))
                u = (t_mid - t0) / max(1e-9, (t1 - t0))
                val += lerp(s0, s1, clamp(u, 0, 1))
                any_cover = True

        if any_cover:
            return val

        # If no event covers t_mid, hold the most recent value (or the first start value if before the first event).
        if last_before is not None:
            s0 = float(last_before.get("start", 0.0))
            s1 = float(last_before.get("end", s0))
            return float(s1)
        first = evs[0]
        return float(first.get("start", 0.0))

    segs: List[Seg1D] = []
    prefix = 0.0
    for i in range(len(cut_list) - 1):
        t0, t1 = cut_list[i], cut_list[i + 1]
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

    fathers: List[int] = []
    rot_with_fathers: List[bool] = []

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

        # Compatibility: some RPE charts store speed events at judgeLine-level instead of inside eventLayers.
        # If we don't find any layer speed events, scroll will be constant 0 and all notes will stick to the judge line.
        if not speed_layers or all((not (ly or [])) for ly in speed_layers):
            jl_speed = jl.get("speedEvents", None)
            if isinstance(jl_speed, list) and jl_speed:
                speed_layers = [jl_speed]

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

        ext = jl.get("extended", {}) or {}
        color_track = None
        try:
            ce = ext.get("colorEvents", None)
            if isinstance(ce, list) and ce:
                color_track = build_rpe_color_track(ce, bpm_map, bpmfactor, default=rgb)
        except:
            color_track = None

        scale_x_track = None
        scale_y_track = None
        try:
            sxev = ext.get("scaleXEvents", None)
            if isinstance(sxev, list) and sxev:
                scale_x_track = build_rpe_eased_track(sxev, bpm_map, bpmfactor, default=1.0)
        except:
            scale_x_track = None
        try:
            syev = ext.get("scaleYEvents", None)
            if isinstance(syev, list) and syev:
                scale_y_track = build_rpe_eased_track(syev, bpm_map, bpmfactor, default=1.0)
        except:
            scale_y_track = None

        text_track = None
        try:
            tev = ext.get("textEvents", None)
            if isinstance(tev, list) and tev:
                text_track = build_rpe_text_track(tev, bpm_map, bpmfactor)
        except:
            text_track = None

        gif_track = None
        try:
            gev = ext.get("gifEvents", None)
            if isinstance(gev, list) and gev:
                gif_track = build_rpe_eased_track(gev, bpm_map, bpmfactor, default=0.0)
        except:
            gif_track = None

        tex_path = None
        try:
            tp = jl.get("Texture", None)
            if tp is not None:
                tp = str(tp)
                if tp and tp != "line.png":
                    tex_path = tp
        except:
            tex_path = None

        anchor = (0.5, 0.5)
        try:
            av = jl.get("anchor", None)
            if isinstance(av, (list, tuple)) and len(av) >= 2:
                anchor = (float(av[0]), float(av[1]))
        except:
            anchor = (0.5, 0.5)

        is_gif = False
        try:
            is_gif = bool(jl.get("isGif", False))
        except:
            is_gif = False

        father = -1
        try:
            father = int(jl.get("father", -1))
        except:
            father = -1

        rotate_with_father = True
        try:
            rotate_with_father = bool(jl.get("rotateWithFather", True))
        except:
            rotate_with_father = True

        fathers.append(int(father))
        rot_with_fathers.append(bool(rotate_with_father))

        lines_out.append(
            RuntimeLine(
                lid=i,
                pos_x=pos_x,
                pos_y=pos_y,
                rot=rot,
                alpha=alpha01,
                scroll_px=scroll,
                color_rgb=rgb,
                color=color_track,
                scale_x=scale_x_track,
                scale_y=scale_y_track,
                text=text_track,
                texture_path=tex_path,
                anchor=anchor,
                is_gif=is_gif,
                gif_progress=gif_track,
                father=int(father),
                rotate_with_father=bool(rotate_with_father),
                name=name,
                event_counts=evc,
            )
        )

        # notes
        nid_base = i * 100000
        nid = nid_base
        for n in (jl.get("notes", []) or []):
            # RPE note type mapping:
            # 1 Tap, 2 Hold, 3 Flick, 4 Drag (default: Tap)
            # Internal mapping:
            # 1 Tap, 2 Drag, 3 Hold, 4 Flick
            try:
                rpe_type = int(n.get("type", 1))
            except:
                rpe_type = 1
            if rpe_type == 2:
                kind = 3
            elif rpe_type == 3:
                kind = 4
            elif rpe_type == 4:
                kind = 2
            else:
                kind = 1
            b0 = beat_to_value(n.get("startTime", [0, 0, 1]))
            b1 = beat_to_value(n.get("endTime", n.get("startTime", [0, 0, 1])))
            t_hit = bpm_map.beat_to_sec(b0, bpmfactor)
            t_end = bpm_map.beat_to_sec(b1, bpmfactor)

            # Some exporters may use non-standard type ids while still providing a distinct endTime.
            # Treat any note with duration as hold.
            if float(t_end) > float(t_hit) + 1e-9:
                kind = 3

            # RPE semantics: above=1 means "front" side; 0 means "back".
            # Many charts also use 2 for hold notes falling from the back side.
            # Our internal note.above currently corresponds to the opposite fall direction, so invert here.
            try:
                above_raw = int(n.get("above", 1))
            except:
                above_raw = 1
            above = (above_raw != 1)
            fake = int(n.get("isFake", 0)) == 1

            posx_units = float(n.get("positionX", 0.0))
            y_offset_units = float(n.get("yOffset", 0.0))
            size = float(n.get("size", 1.0))
            speed_mul = float(n.get("speed", 1.0))

            na = n.get("alpha", None)
            alpha_note = 1.0 if na is None else clamp(float(na) / 255.0, 0.0, 1.0)

            hs = n.get("hitsound", None)  # relative to chart root directory

            tint_val = n.get("tint", None)
            if tint_val is None:
                tint_val = n.get("color", None)
            tint_rgb = _parse_rgb3(tint_val) if tint_val is not None else (255, 255, 255)

            tint_hitfx_rgb = _parse_rgb3_opt(n.get("tintHitEffects", None))

            note = RuntimeNote(
                nid=nid,
                line_id=i,
                kind=kind,
                above=above,
                fake=fake,
                t_hit=t_hit,
                t_end=t_end if kind == 3 else t_hit,  # only hold uses end
                x_local_px=posx_units * sx,  # local units -> px
                y_offset_px=y_offset_units * sy,
                speed_mul=speed_mul,
                size_px=size,
                alpha01=alpha_note,
                tint_rgb=tint_rgb,
                tint_hitfx_rgb=tint_hitfx_rgb,
                hitsound_path=hs,
            )
            notes_out.append(note)
            nid += 1

    # cache scroll samples
    line_map = {ln.lid: ln for ln in lines_out}
    for n in notes_out:
        ln = line_map[n.line_id]
        n.scroll_hit = ln.scroll_px.integral(n.t_hit)
        n.scroll_end = ln.scroll_px.integral(n.t_end)

    # Compose father/child judge lines (position always sums; rotation sums only when rotateWithFather is true).
    base_x = [ln.pos_x for ln in lines_out]
    base_y = [ln.pos_y for ln in lines_out]
    base_r = [ln.rot for ln in lines_out]

    state_mark = [0] * len(lines_out)  # 0=unvisited,1=visiting,2=done
    cache: Dict[int, Tuple[Any, Any, Any]] = {}

    def _build_comp(lid: int) -> Tuple[Any, Any, Any]:
        if lid in cache:
            return cache[lid]
        if lid < 0 or lid >= len(lines_out):
            z = (lambda t: 0.0, lambda t: 0.0, lambda t: 0.0)
            return z
        if state_mark[lid] == 1:
            raise ValueError(f"RPE father cycle detected at line {lid}")
        if state_mark[lid] == 2:
            return cache[lid]
        state_mark[lid] = 1

        f = fathers[lid] if lid < len(fathers) else -1
        if f is None:
            f = -1
        try:
            f = int(f)
        except:
            f = -1

        bx = base_x[lid]
        by = base_y[lid]
        br = base_r[lid]

        if f < 0 or f >= len(lines_out):
            x = bx
            y = by
            r = br
        else:
            px, py, pr = _build_comp(f)
            x = (lambda t, bx=bx, px=px: float(bx(t)) + float(px(t)))
            y = (lambda t, by=by, py=py: float(by(t)) + float(py(t)))
            if rot_with_fathers[lid]:
                r = (lambda t, br=br, pr=pr: float(br(t)) + float(pr(t)))
            else:
                r = br

        cache[lid] = (x, y, r)
        state_mark[lid] = 2
        return cache[lid]

    for lid in range(len(lines_out)):
        x, y, r = _build_comp(lid)
        lines_out[lid].pos_x = x
        lines_out[lid].pos_y = y
        lines_out[lid].rot = r

    return offset, lines_out, sorted(notes_out, key=lambda x: x.t_hit)
