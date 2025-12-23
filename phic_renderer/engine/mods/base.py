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

    return True


def apply_note_set(n: RuntimeNote, st: dict[str, Any]):
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
