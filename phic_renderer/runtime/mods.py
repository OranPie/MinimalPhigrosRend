from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from .. import state
from ..types import RuntimeLine, RuntimeNote
from ..math.util import clamp


def apply_mods(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    if not isinstance(mods_cfg, dict) or not mods_cfg:
        return notes

    def _parse_kind(v: Any) -> Optional[int]:
        if v is None:
            return None
        if isinstance(v, int):
            return int(v)
        if isinstance(v, float):
            return int(v)
        s = str(v).strip().lower()
        if not s:
            return None
        # Common names
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

    def _parse_alpha01(v: Any) -> Optional[float]:
        if v is None:
            return None
        try:
            fv = float(v)
        except:
            return None
        if fv <= 1.000001:
            return clamp(fv, 0.0, 1.0)
        return clamp(fv / 255.0, 0.0, 1.0)

    def _parse_int(v: Any) -> Optional[int]:
        if v is None:
            return None
        try:
            return int(v)
        except:
            return None

    def _parse_float(v: Any) -> Optional[float]:
        if v is None:
            return None
        try:
            return float(v)
        except:
            return None

    def _parse_rgb(v: Any) -> Optional[Tuple[int, int, int]]:
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

    def _apply_note_side(note: RuntimeNote, side: Any):
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

    def _as_list(v: Any) -> List[Any]:
        if v is None:
            return []
        if isinstance(v, list):
            return v
        if isinstance(v, tuple):
            return list(v)
        return [v]

    def _match_note_filter(n: RuntimeNote, flt: Dict[str, Any]) -> bool:
        lids = _as_list(flt.get("line_id", flt.get("line_ids", None)))
        if lids:
            try:
                lids_i = set(int(x) for x in lids)
                if int(n.line_id) not in lids_i:
                    return False
            except:
                return False

        kinds = _as_list(flt.get("kind", flt.get("kinds", None)))
        if kinds:
            try:
                ks: set[int] = set()
                for x in kinds:
                    kx = _parse_kind(x)
                    if kx is not None:
                        ks.add(int(kx))
                if int(n.kind) not in ks:
                    return False
            except:
                return False

        not_kinds = _as_list(flt.get("not_kind", flt.get("exclude_kind", flt.get("not_kinds", None))))
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

    def _apply_note_set(n: RuntimeNote, st: Dict[str, Any]):
        if "kind" in st and st.get("kind") is not None:
            try:
                kk = _parse_kind(st.get("kind"))
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
            a01 = _parse_alpha01(st.get("alpha"))
            if a01 is not None:
                n.alpha01 = a01
        if "size" in st and st.get("size") is not None:
            try:
                n.size_px = float(st.get("size"))
            except:
                pass
        if "side" in st:
            _apply_note_side(n, st.get("side"))
        elif "above" in st:
            _apply_note_side(n, st.get("above"))

    def _match_line_filter(ln: RuntimeLine, flt: Dict[str, Any]) -> bool:
        lids = _as_list(flt.get("lid", flt.get("line_id", flt.get("line_ids", flt.get("lids", None)))))
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

    def _apply_line_set(ln: RuntimeLine, st: Dict[str, Any]):
        if "color" in st and st.get("color") is not None:
            rgb = _parse_rgb(st.get("color"))
            if rgb is not None:
                ln.color_rgb = rgb
        if "name" in st and st.get("name") is not None:
            try:
                ln.name = str(st.get("name"))
            except:
                pass
        if "force_alpha" in st and st.get("force_alpha") is not None:
            a01 = _parse_alpha01(st.get("force_alpha"))
            if a01 is not None:
                if state.force_line_alpha01_by_lid is None:
                    state.force_line_alpha01_by_lid = {}
                state.force_line_alpha01_by_lid[int(ln.lid)] = a01

    gla = mods_cfg.get("force_line_alpha", None)
    if gla is not None:
        a01 = _parse_alpha01(gla)
        if a01 is not None:
            state.force_line_alpha01 = a01

    if bool(mods_cfg.get("note_speed_mul_affects_travel", False)):
        state.note_speed_mul_affects_travel = True

    visual_cfg = None
    for k in ("visual", "render", "renderer"):
        if k in mods_cfg and isinstance(mods_cfg.get(k), dict):
            visual_cfg = mods_cfg.get(k)
            break

    if isinstance(visual_cfg, dict) and visual_cfg:
        ov = _parse_float(visual_cfg.get("overrender", visual_cfg.get("render_scale", None)))
        if ov is not None:
            state.render_overrender = max(1.0, float(ov))

        trail_cfg = visual_cfg.get("trail", None)
        if isinstance(trail_cfg, dict) and bool(trail_cfg.get("enable", True)):
            a = _parse_float(trail_cfg.get("alpha", trail_cfg.get("trail_alpha", None)))
            if a is not None:
                state.trail_alpha = clamp(float(a), 0.0, 1.0)
            fr = _parse_int(trail_cfg.get("frames", trail_cfg.get("trail_frames", None)))
            if fr is not None:
                state.trail_frames = max(1, int(fr))
            dec = _parse_float(trail_cfg.get("decay", None))
            if dec is not None:
                state.trail_decay = clamp(float(dec), 0.0, 1.0)
            bl = _parse_int(trail_cfg.get("blur", trail_cfg.get("trail_blur", None)))
            if bl is not None:
                state.trail_blur = max(0, int(bl))
            dm = _parse_int(trail_cfg.get("dim", trail_cfg.get("trail_dim", None)))
            if dm is not None:
                state.trail_dim = clamp(int(dm), 0, 255)
            if "blur_ramp" in trail_cfg:
                try:
                    state.trail_blur_ramp = bool(trail_cfg.get("blur_ramp"))
                except:
                    pass
            if "blend" in trail_cfg and trail_cfg.get("blend") is not None:
                try:
                    state.trail_blend = str(trail_cfg.get("blend")).strip().lower()
                except:
                    pass

        mb_cfg = visual_cfg.get("motion_blur", visual_cfg.get("motionblur", None))
        if isinstance(mb_cfg, dict) and bool(mb_cfg.get("enable", True)):
            smp = _parse_int(mb_cfg.get("samples", mb_cfg.get("n", None)))
            if smp is not None:
                state.motion_blur_samples = max(1, int(smp))
            shu = _parse_float(mb_cfg.get("shutter", None))
            if shu is not None:
                state.motion_blur_shutter = clamp(float(shu), 0.0, 2.0)

    ov2 = _parse_float(mods_cfg.get("overrender", None))
    if ov2 is not None:
        state.render_overrender = max(1.0, float(ov2))

    a2 = _parse_float(mods_cfg.get("trail_alpha", None))
    if a2 is not None:
        state.trail_alpha = clamp(float(a2), 0.0, 1.0)
    fr2 = _parse_int(mods_cfg.get("trail_frames", None))
    if fr2 is not None:
        state.trail_frames = max(1, int(fr2))
    dec2 = _parse_float(mods_cfg.get("trail_decay", None))
    if dec2 is not None:
        state.trail_decay = clamp(float(dec2), 0.0, 1.0)
    bl2 = _parse_int(mods_cfg.get("trail_blur", None))
    if bl2 is not None:
        state.trail_blur = max(0, int(bl2))
    dm2 = _parse_int(mods_cfg.get("trail_dim", None))
    if dm2 is not None:
        state.trail_dim = clamp(int(dm2), 0, 255)
    if "trail_blur_ramp" in mods_cfg:
        try:
            state.trail_blur_ramp = bool(mods_cfg.get("trail_blur_ramp"))
        except:
            pass
    if "trail_blend" in mods_cfg and mods_cfg.get("trail_blend") is not None:
        try:
            state.trail_blend = str(mods_cfg.get("trail_blend")).strip().lower()
        except:
            pass

    mb_s = _parse_int(mods_cfg.get("motion_blur_samples", mods_cfg.get("mb_samples", None)))
    if mb_s is not None:
        state.motion_blur_samples = max(1, int(mb_s))
    mb_sh = _parse_float(mods_cfg.get("motion_blur_shutter", mods_cfg.get("mb_shutter", None)))
    if mb_sh is not None:
        state.motion_blur_shutter = clamp(float(mb_sh), 0.0, 2.0)

    full_blue_cfg = None
    for k in ("full_blue", "full_blue_mode", "fullbluemode", "FullBlueMode"):
        if k in mods_cfg:
            full_blue_cfg = mods_cfg.get(k)
            break

    if isinstance(full_blue_cfg, dict) and bool(full_blue_cfg.get("enable", True)):
        la_force = full_blue_cfg.get("force_line_alpha", 255)
        a01 = _parse_alpha01(la_force)
        if a01 is not None:
            state.force_line_alpha01 = a01

        if bool(full_blue_cfg.get("note_speed_mul_affects_travel", True)):
            state.note_speed_mul_affects_travel = True

        if bool(full_blue_cfg.get("convert_non_hold_to_tap", True)):
            for n in notes:
                if n.kind != 3:
                    n.kind = 1

        note_ov = full_blue_cfg.get("note_overrides", {})
        if isinstance(note_ov, dict) and note_ov:
            apply_to_hold = bool(note_ov.get("apply_to_hold", True))
            force_speed = note_ov.get("speed_mul", None)
            force_alpha = note_ov.get("alpha", None)
            force_side = note_ov.get("side", None)
            force_size = note_ov.get("size", None)

            alpha01_force = _parse_alpha01(force_alpha)
            try:
                speed_force = None if force_speed is None else float(force_speed)
            except:
                speed_force = None
            try:
                size_force = None if force_size is None else float(force_size)
            except:
                size_force = None

            for n in notes:
                if (not apply_to_hold) and n.kind == 3:
                    continue
                if speed_force is not None:
                    n.speed_mul = speed_force
                if alpha01_force is not None:
                    n.alpha01 = alpha01_force
                if size_force is not None:
                    n.size_px = size_force
                _apply_note_side(n, force_side)

    hold_to_tap_drag_cfg = None
    for k in (
        "hold_to_tap_drag",
        "hold_to_tap_drag_mode",
        "hold_to_tap_and_drag",
        "hold_convert",
    ):
        if k in mods_cfg:
            hold_to_tap_drag_cfg = mods_cfg.get(k)
            break

    if isinstance(hold_to_tap_drag_cfg, dict) and bool(hold_to_tap_drag_cfg.get("enable", True)):
        try:
            interval = float(hold_to_tap_drag_cfg.get("drag_interval", hold_to_tap_drag_cfg.get("interval", 0.1)))
        except:
            interval = 0.1
        interval = max(1e-4, interval)

        include_end = bool(hold_to_tap_drag_cfg.get("include_end", True))
        tap_head = bool(hold_to_tap_drag_cfg.get("tap_head", True))
        remove_hold = bool(hold_to_tap_drag_cfg.get("remove_hold", True))
        drag_kind = _parse_kind(hold_to_tap_drag_cfg.get("drag_kind", 2))
        if drag_kind is None:
            drag_kind = 2

        nid_next = max((int(n.nid) for n in notes), default=0) + 1
        line_map = {int(ln.lid): ln for ln in lines}
        out_notes: List[RuntimeNote] = []

        for n in notes:
            if n.kind != 3 or n.fake or (n.t_end <= n.t_hit + 1e-6):
                out_notes.append(n)
                continue

            ln = line_map.get(int(n.line_id))
            if ln is None:
                out_notes.append(n)
                continue

            if not remove_hold:
                out_notes.append(n)

            if tap_head:
                tap_nid = int(n.nid) if remove_hold else int(nid_next)
                if not remove_hold:
                    nid_next += 1
                tap = RuntimeNote(
                    nid=tap_nid,
                    line_id=int(n.line_id),
                    kind=1,
                    above=bool(n.above),
                    fake=False,
                    t_hit=float(n.t_hit),
                    t_end=float(n.t_hit),
                    x_local_px=float(n.x_local_px),
                    y_offset_px=float(n.y_offset_px),
                    speed_mul=float(n.speed_mul),
                    size_px=float(n.size_px),
                    alpha01=float(n.alpha01),
                    hitsound_path=n.hitsound_path,
                    t_enter=-1e9,
                    mh=False,
                )
                tap.scroll_hit = ln.scroll_px.integral(tap.t_hit)
                tap.scroll_end = tap.scroll_hit
                out_notes.append(tap)

            t0 = float(n.t_hit) + interval
            t_end = float(n.t_end)
            last_drag_t: Optional[float] = None
            while t0 < t_end - 1e-9:
                dn = RuntimeNote(
                    nid=int(nid_next),
                    line_id=int(n.line_id),
                    kind=int(drag_kind),
                    above=bool(n.above),
                    fake=False,
                    t_hit=float(t0),
                    t_end=float(t0),
                    x_local_px=float(n.x_local_px),
                    y_offset_px=float(n.y_offset_px),
                    speed_mul=float(n.speed_mul),
                    size_px=float(n.size_px),
                    alpha01=float(n.alpha01),
                    hitsound_path=n.hitsound_path,
                    t_enter=-1e9,
                    mh=False,
                )
                dn.scroll_hit = ln.scroll_px.integral(dn.t_hit)
                dn.scroll_end = dn.scroll_hit
                out_notes.append(dn)
                last_drag_t = float(t0)
                nid_next += 1
                t0 += interval

            if include_end and t_end > float(n.t_hit) + 1e-9:
                if (last_drag_t is None) or (abs(float(last_drag_t) - float(t_end)) > interval * 0.5):
                    dn = RuntimeNote(
                        nid=int(nid_next),
                        line_id=int(n.line_id),
                        kind=int(drag_kind),
                        above=bool(n.above),
                        fake=False,
                        t_hit=float(t_end),
                        t_end=float(t_end),
                        x_local_px=float(n.x_local_px),
                        y_offset_px=float(n.y_offset_px),
                        speed_mul=float(n.speed_mul),
                        size_px=float(n.size_px),
                        alpha01=float(n.alpha01),
                        hitsound_path=n.hitsound_path,
                        t_enter=-1e9,
                        mh=False,
                    )
                    dn.scroll_hit = ln.scroll_px.integral(dn.t_hit)
                    dn.scroll_end = dn.scroll_hit
                    out_notes.append(dn)
                    nid_next += 1

        notes = sorted(out_notes, key=lambda x: x.t_hit)

    rules_raw = mods_cfg.get("note_rules", mods_cfg.get("rules", None))
    if isinstance(rules_raw, list) and rules_raw:
        for rule in rules_raw:
            if not isinstance(rule, dict):
                continue
            flt = rule.get("filter", rule.get("when", {}))
            st = rule.get("set", rule.get("then", {}))
            if not isinstance(flt, dict) or not isinstance(st, dict):
                continue
            apply_to_hold = bool(rule.get("apply_to_hold", True))
            for n in notes:
                if (not apply_to_hold) and n.kind == 3:
                    continue
                if _match_note_filter(n, flt):
                    _apply_note_set(n, st)

    glob_no = mods_cfg.get("note_overrides", None)
    if isinstance(glob_no, dict) and glob_no:
        apply_to_hold = bool(glob_no.get("apply_to_hold", True))
        st = dict(glob_no.get("set", glob_no))
        for n in notes:
            if (not apply_to_hold) and n.kind == 3:
                continue
            _apply_note_set(n, st)

    lr_raw = mods_cfg.get("line_rules", None)
    if isinstance(lr_raw, list) and lr_raw:
        for rule in lr_raw:
            if not isinstance(rule, dict):
                continue
            flt = rule.get("filter", rule.get("when", {}))
            st = rule.get("set", rule.get("then", {}))
            if not isinstance(flt, dict) or not isinstance(st, dict):
                continue
            for ln in lines:
                if _match_line_filter(ln, flt):
                    _apply_line_set(ln, st)

    return notes
