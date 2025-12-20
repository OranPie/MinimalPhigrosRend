from __future__ import annotations

from typing import Any, Dict, List, Optional

from ...types import RuntimeLine, RuntimeNote
from .base import parse_kind


def apply_hold_to_tap_drag(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
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

    if not (isinstance(hold_to_tap_drag_cfg, dict) and bool(hold_to_tap_drag_cfg.get("enable", True))):
        return notes

    try:
        interval = float(hold_to_tap_drag_cfg.get("drag_interval", hold_to_tap_drag_cfg.get("interval", 0.1)))
    except:
        interval = 0.1
    interval = max(1e-4, interval)

    include_end = bool(hold_to_tap_drag_cfg.get("include_end", True))
    tap_head = bool(hold_to_tap_drag_cfg.get("tap_head", True))
    remove_hold = bool(hold_to_tap_drag_cfg.get("remove_hold", True))
    drag_kind = parse_kind(hold_to_tap_drag_cfg.get("drag_kind", 2))
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

    return sorted(out_notes, key=lambda x: x.t_hit)
