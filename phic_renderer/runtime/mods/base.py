from __future__ import annotations

from typing import Any, List, Optional, Tuple

from ...math.util import clamp
from ...types import RuntimeLine, RuntimeNote


def parse_kind(v: Any) -> Optional[int]:
    if v is None:
        return None
    if isinstance(v, int):
        return int(v)
    if isinstance(v, float):
        return int(v)
    s = str(v).strip().lower()
    if not s:
        return None
    if s in {"tap", "click", "note", "n1", "1"}:
        return 1
    if s in {"drag", "slide", "n2", "2"}:
        return 2
    if s in {"hold", "long", "n3", "3"}:
        return 3
    if s in {"flick", "flk", "n4", "4"}:
        return 4
    try:
        return int(s)
    except:
        return None


def parse_alpha01(v: Any) -> Optional[float]:
    if v is None:
        return None
    try:
        fv = float(v)
    except:
        return None
    if fv <= 1.000001:
        return clamp(fv, 0.0, 1.0)
    return clamp(fv / 255.0, 0.0, 1.0)


def parse_int(v: Any) -> Optional[int]:
    if v is None:
        return None
    try:
        return int(v)
    except:
        return None


def parse_float(v: Any) -> Optional[float]:
    if v is None:
        return None
    try:
        return float(v)
    except:
        return None


def parse_rgb(v: Any) -> Optional[Tuple[int, int, int]]:
    if v is None:
        return None
    if isinstance(v, (list, tuple)) and len(v) >= 3:
        try:
            r, g, b = int(v[0]), int(v[1]), int(v[2])
            return (clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255))
        except:
            return None
    if isinstance(v, str):
        s = v.strip().lstrip("#")
        if len(s) == 6:
            try:
                r = int(s[0:2], 16)
                g = int(s[2:4], 16)
                b = int(s[4:6], 16)
                return (r, g, b)
            except:
                return None
    return None


def apply_note_side(note: RuntimeNote, side: Any):
    if side is None:
        return
    if isinstance(side, bool):
        note.above = bool(side)
        return
    s = str(side).strip().lower()
    if s in {"above", "up", "top", "1", "true"}:
        note.above = True
    elif s in {"below", "down", "bottom", "0", "false"}:
        note.above = False
    elif s in {"flip", "toggle", "invert"}:
        note.above = not bool(note.above)


def as_list(v: Any) -> List[Any]:
    if v is None:
        return []
    if isinstance(v, list):
        return v
    if isinstance(v, tuple):
        return list(v)
    return [v]


def match_note_filter(n: RuntimeNote, flt: dict[str, Any]) -> bool:
    """Enhanced note filter with more matching criteria.

    Supports: line_id, kinds, not_kinds, above, fake, time ranges,
    x position ranges, y offset ranges, speed ranges, size ranges,
    note ID ranges, modulo patterns, and random sampling.
    """
    lids = as_list(flt.get("line_id", flt.get("line_ids", None)))
    if lids:
        try:
            lids_i = set(int(x) for x in lids)
            if int(n.line_id) not in lids_i:
                return False
        except:
            return False

    kinds = as_list(flt.get("kind", flt.get("kinds", None)))
    if kinds:
        try:
            ks: set[int] = set()
            for x in kinds:
                kx = parse_kind(x)
                if kx is not None:
                    ks.add(int(kx))
            if int(n.kind) not in ks:
                return False
        except:
            return False

    not_kinds = as_list(flt.get("not_kind", flt.get("exclude_kind", flt.get("not_kinds", None))))
    if not_kinds:
        try:
            nks = set(int(x) for x in not_kinds)
            if int(n.kind) in nks:
                return False
        except:
            return False

    if "above" in flt:
        try:
            if bool(flt.get("above")) != bool(n.above):
                return False
        except:
            return False

    if "fake" in flt:
        try:
            if bool(flt.get("fake")) != bool(n.fake):
                return False
        except:
            return False

    # Time ranges
    tmin = flt.get("t_hit_min", flt.get("time_min", None))
    tmax = flt.get("t_hit_max", flt.get("time_max", None))
    if tmin is not None:
        try:
            if float(n.t_hit) < float(tmin):
                return False
        except:
            return False
    if tmax is not None:
        try:
            if float(n.t_hit) > float(tmax):
                return False
        except:
            return False

    emin = flt.get("t_end_min", None)
    emax = flt.get("t_end_max", None)
    if emin is not None:
        try:
            if float(n.t_end) < float(emin):
                return False
        except:
            return False
    if emax is not None:
        try:
            if float(n.t_end) > float(emax):
                return False
        except:
            return False

    # X position ranges
    xmin = flt.get("x_min", flt.get("x_local_min", None))
    xmax = flt.get("x_max", flt.get("x_local_max", None))
    if xmin is not None:
        try:
            if float(n.x_local_px) < float(xmin):
                return False
        except:
            return False
    if xmax is not None:
        try:
            if float(n.x_local_px) > float(xmax):
                return False
        except:
            return False

    # Y offset ranges
    ymin = flt.get("y_min", flt.get("y_offset_min", None))
    ymax = flt.get("y_max", flt.get("y_offset_max", None))
    if ymin is not None:
        try:
            if float(n.y_offset_px) < float(ymin):
                return False
        except:
            return False
    if ymax is not None:
        try:
            if float(n.y_offset_px) > float(ymax):
                return False
        except:
            return False

    # Speed multiplier ranges
    speed_min = flt.get("speed_min", flt.get("speed_mul_min", None))
    speed_max = flt.get("speed_max", flt.get("speed_mul_max", None))
    if speed_min is not None:
        try:
            if float(n.speed_mul) < float(speed_min):
                return False
        except:
            return False
    if speed_max is not None:
        try:
            if float(n.speed_mul) > float(speed_max):
                return False
        except:
            return False

    # Size ranges
    size_min = flt.get("size_min", None)
    size_max = flt.get("size_max", None)
    if size_min is not None:
        try:
            if float(n.size_px) < float(size_min):
                return False
        except:
            return False
    if size_max is not None:
        try:
            if float(n.size_px) > float(size_max):
                return False
        except:
            return False

    # Note ID ranges and patterns
    nid_min = flt.get("nid_min", flt.get("note_id_min", None))
    nid_max = flt.get("nid_max", flt.get("note_id_max", None))
    if nid_min is not None:
        try:
            if int(n.nid) < int(nid_min):
                return False
        except:
            return False
    if nid_max is not None:
        try:
            if int(n.nid) > int(nid_max):
                return False
        except:
            return False

    # Modulo pattern: e.g., {"every": 2, "offset": 0} = every 2nd note starting at 0
    every = flt.get("every", flt.get("modulo", None))
    if every is not None:
        try:
            every_i = int(every)
            offset_i = int(flt.get("offset", flt.get("modulo_offset", 0)))
            if (int(n.nid) - offset_i) % every_i != 0:
                return False
        except:
            return False

    # Random sampling: e.g., {"probability": 0.5} = 50% chance
    prob = flt.get("probability", flt.get("random", None))
    if prob is not None:
        try:
            import random
            # Use note ID as seed for deterministic randomness
            random.seed(int(n.nid) * 31337)
            if random.random() > float(prob):
                return False
        except:
            return False

    return True


def apply_note_set(n: RuntimeNote, st: dict[str, Any]):
    """Enhanced note setter with more modification capabilities.

    Supports: kind, speed_mul, alpha, size, side/above, x/y offsets,
    tint colors, fake flag, and time offsets.
    """
    if "kind" in st and st.get("kind") is not None:
        try:
            kk = parse_kind(st.get("kind"))
            if kk is not None:
                n.kind = int(kk)
        except:
            pass

    if "speed_mul" in st and st.get("speed_mul") is not None:
        try:
            n.speed_mul = float(st.get("speed_mul"))
        except:
            pass
    elif "speed" in st and st.get("speed") is not None:
        try:
            n.speed_mul = float(st.get("speed"))
        except:
            pass

    if "alpha" in st and st.get("alpha") is not None:
        a01 = parse_alpha01(st.get("alpha"))
        if a01 is not None:
            n.alpha01 = a01

    if "size" in st and st.get("size") is not None:
        try:
            n.size_px = float(st.get("size"))
        except:
            pass

    if "side" in st:
        apply_note_side(n, st.get("side"))
    elif "above" in st:
        apply_note_side(n, st.get("above"))

    # X position offset (additive)
    if "x_offset" in st and st.get("x_offset") is not None:
        try:
            n.x_local_px = float(n.x_local_px) + float(st.get("x_offset"))
        except:
            pass
    # X position absolute (set)
    elif "x" in st and st.get("x") is not None:
        try:
            n.x_local_px = float(st.get("x"))
        except:
            pass

    # Y offset (additive)
    if "y_offset" in st and st.get("y_offset") is not None:
        try:
            n.y_offset_px = float(n.y_offset_px) + float(st.get("y_offset"))
        except:
            pass
    # Y position absolute (set)
    elif "y" in st and st.get("y") is not None:
        try:
            n.y_offset_px = float(st.get("y"))
        except:
            pass

    # Time offset (additive)
    if "time_offset" in st and st.get("time_offset") is not None:
        try:
            offset = float(st.get("time_offset"))
            n.t_hit = float(n.t_hit) + offset
            n.t_end = float(n.t_end) + offset
        except:
            pass

    # Tint color
    if "tint" in st and st.get("tint") is not None:
        rgb = parse_rgb(st.get("tint"))
        if rgb is not None:
            n.tint_rgb = rgb
    if "tint_hitfx" in st and st.get("tint_hitfx") is not None:
        rgb = parse_rgb(st.get("tint_hitfx"))
        if rgb is not None:
            n.tint_hitfx_rgb = rgb

    # Fake flag
    if "fake" in st and st.get("fake") is not None:
        try:
            n.fake = bool(st.get("fake"))
        except:
            pass


def match_line_filter(ln: RuntimeLine, flt: dict[str, Any]) -> bool:
    lids = as_list(flt.get("lid", flt.get("line_id", flt.get("line_ids", flt.get("lids", None)))))
    if lids:
        try:
            lids_i = set(int(x) for x in lids)
            if int(ln.lid) not in lids_i:
                return False
        except:
            return False
    if "name" in flt and flt.get("name") is not None:
        try:
            if str(flt.get("name")) != str(getattr(ln, "name", "")):
                return False
        except:
            return False
    return True


def apply_line_set(ln: RuntimeLine, st: dict[str, Any]):
    from ... import state
    
    if "color" in st and st.get("color") is not None:
        rgb = parse_rgb(st.get("color"))
        if rgb is not None:
            ln.color_rgb = rgb
    if "name" in st and st.get("name") is not None:
        try:
            ln.name = str(st.get("name"))
        except:
            pass
    if "force_alpha" in st and st.get("force_alpha") is not None:
        a01 = parse_alpha01(st.get("force_alpha"))
        if a01 is not None:
            if state.force_line_alpha01_by_lid is None:
                state.force_line_alpha01_by_lid = {}
            state.force_line_alpha01_by_lid[int(ln.lid)] = a01
