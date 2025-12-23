from __future__ import annotations

import bisect
from typing import Any, Dict, List, Optional

from ...math.util import clamp, now_sec
from ...core.ui import compute_score
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...backends.pygame.utils.rendering import line_note_counts_kind, track_seg_state


def render_curses_ui(
    cui: Any,
    curses_mod: Any,
    cui_has_color: bool,
    cui_view: int,
    cui_scroll: int,
    *,
    t: float,
    record_frame_idx: int,
    record_start_time: float,
    record_end_time: Optional[float],
    record_fps: float,
    record_wall_t0: float,
    record_curses_fps: float,
    chart_end: float,
    judge: Any,
    total_notes: int,
    particles_count: int,
    note_times_by_kind: Dict[int, List[float]],
    note_times_by_line_kind: Dict[int, Dict[int, List[float]]],
    approach: float,
    args: Any,
    events_incoming: Optional[List[str]] = None,
    events_past: Optional[List[str]] = None,
    lines: Optional[List[Any]] = None,
    notes: Optional[List[Any]] = None,
    states: Optional[List[Any]] = None,
    idx_next: Optional[int] = None,
    W: Optional[int] = None,
    H: Optional[int] = None,
) -> int:
    """Render the curses UI for recording mode. Returns updated cui_scroll."""
    if cui is None or curses_mod is None:
        return cui_scroll

    try:
        end_for_prog = float(record_end_time) if record_end_time is not None else float(chart_end)
        denom = max(1e-6, float(end_for_prog) - float(record_start_time))
        ratio = clamp((float(t) - float(record_start_time)) / denom, 0.0, 1.0)

        wall_elapsed = max(1e-6, float(now_sec() - float(record_wall_t0)))
        fps_wall = float(record_frame_idx) / float(wall_elapsed)
        speed = float(fps_wall) / max(1e-6, float(record_fps))
        frames_total = max(0.0, float(denom) * float(record_fps))
        frames_left = max(0.0, float(frames_total) - float(record_frame_idx))
        eta_sec = float(frames_left) / max(1e-6, float(fps_wall))

        past_k = [0, 0, 0, 0]
        inc_k = [0, 0, 0, 0]
        t1 = float(t) + float(approach)
        for kd, arr in note_times_by_kind.items():
            idx = int(kd) - 1
            if idx < 0 or idx >= 4:
                continue
            p = bisect.bisect_left(arr, float(t))
            q = bisect.bisect_right(arr, float(t1)) - p
            past_k[idx] = int(p)
            inc_k[idx] = int(q)

        h, w = cui.getmaxyx()
        
        # Color attributes
        attr_head = attr_ok = attr_warn = attr_bad = attr_dim = 0
        attr_tap = attr_drag = attr_hold = attr_flick = 0
        if curses_mod is not None and bool(cui_has_color):
            try:
                attr_head = int(curses_mod.color_pair(1)) | int(getattr(curses_mod, "A_BOLD", 0))
                attr_ok = int(curses_mod.color_pair(2))
                attr_warn = int(curses_mod.color_pair(3))
                attr_bad = int(curses_mod.color_pair(4))
                attr_tap = int(curses_mod.color_pair(1))
                attr_drag = int(curses_mod.color_pair(2))
                attr_hold = int(curses_mod.color_pair(3))
                attr_flick = int(curses_mod.color_pair(5))
                attr_dim = int(curses_mod.color_pair(6))
            except:
                pass

        try:
            cui.erase()
        except:
            pass

        # Header line
        head = f"[record] {ratio*100:6.2f}%  t={float(t):.3f}/{float(end_for_prog):.3f}s  frame={record_frame_idx:7d}  {fps_wall:6.1f}fps  x{speed:4.2f}  ETA {eta_sec:6.1f}s"
        try:
            cui.addnstr(0, 0, head, max(0, w - 1), attr_head)
        except:
            pass

        # Progress bar
        _render_progress_bar(cui, w, ratio, attr_dim, attr_ok)

        # Note counts and score
        _render_note_counts(
            cui, w, past_k, inc_k, judge, total_notes, particles_count,
            attr_dim, attr_tap, attr_drag, attr_hold, attr_flick
        )

        # Recorder stats
        _render_recorder_stats(cui, w, speed, args, attr_ok, attr_warn, attr_bad, attr_dim)

        if int(cui_view) != 0:
            _render_help_view(cui, h, w)
        else:
            cui_scroll = _render_dashboard_view(
                cui,
                h,
                w,
                cui_scroll,
                t,
                approach,
                note_times_by_line_kind,
                attr_dim,
                attr_head,
                events_incoming,
                events_past,
                lines,
                notes,
                states,
                idx_next,
                W,
                H,
            )

        try:
            cui.refresh()
        except:
            pass

    except:
        pass

    return cui_scroll


def _render_progress_bar(cui: Any, w: int, ratio: float, attr_dim: int, attr_ok: int):
    """Render the progress bar."""
    try:
        bar_w = max(1, int(w) - 2)
        fill = int(round(float(ratio) * float(bar_w)))
        fill = max(0, min(int(bar_w), int(fill)))
        try:
            cui.addnstr(1, 0, "[", 1, attr_dim)
        except:
            pass
        if fill > 0:
            try:
                cui.addnstr(1, 1, "#" * int(fill), max(0, int(fill)), attr_ok)
            except:
                pass
        if (bar_w - fill) > 0:
            try:
                cui.addnstr(1, 1 + int(fill), "-" * int(bar_w - fill), max(0, int(bar_w - fill)), attr_dim)
            except:
                pass
        try:
            if int(w) >= int(bar_w) + 2:
                cui.addnstr(1, 1 + int(bar_w), "]", 1, attr_dim)
        except:
            pass
    except:
        pass


def _render_note_counts(
    cui: Any, w: int, past_k: List[int], inc_k: List[int],
    judge: Any, total_notes: int, particles_count: int,
    attr_dim: int, attr_tap: int, attr_drag: int, attr_hold: int, attr_flick: int
):
    """Render note count statistics."""
    try:
        tot_p = sum(int(x) for x in past_k)
        tot_i = sum(int(x) for x in inc_k)
        score, acc_ratio, _combo_ratio = compute_score(judge.acc_sum, judge.judged_cnt, judge.combo, judge.max_combo, total_notes)
        extra = f" combo={judge.combo} score={score:07d} acc={acc_ratio*100:6.2f}% part={particles_count}"

        x = 0
        cui.addnstr(2, x, f"P {tot_p:6d} (", max(0, w - 1 - x), attr_dim)
        x += len(f"P {tot_p:6d} (")
        cui.addnstr(2, x, f"{past_k[0]:5d}", max(0, w - 1 - x), attr_tap)
        x += len(f"{past_k[0]:5d}")
        cui.addnstr(2, x, "/", max(0, w - 1 - x), attr_dim)
        x += 1
        cui.addnstr(2, x, f"{past_k[1]:5d}", max(0, w - 1 - x), attr_drag)
        x += len(f"{past_k[1]:5d}")
        cui.addnstr(2, x, "/", max(0, w - 1 - x), attr_dim)
        x += 1
        cui.addnstr(2, x, f"{past_k[2]:5d}", max(0, w - 1 - x), attr_hold)
        x += len(f"{past_k[2]:5d}")
        cui.addnstr(2, x, "/", max(0, w - 1 - x), attr_dim)
        x += 1
        cui.addnstr(2, x, f"{past_k[3]:5d}", max(0, w - 1 - x), attr_flick)
        x += len(f"{past_k[3]:5d}")
        cui.addnstr(2, x, ")  I ", max(0, w - 1 - x), attr_dim)
        x += len(")  I ")
        cui.addnstr(2, x, f"{tot_i:6d} (")
        x += len(f"{tot_i:6d} (")
        cui.addnstr(2, x, f"{inc_k[0]:5d}", max(0, w - 1 - x), attr_tap)
        x += len(f"{inc_k[0]:5d}")
        cui.addnstr(2, x, "/", max(0, w - 1 - x), attr_dim)
        x += 1
        cui.addnstr(2, x, f"{inc_k[1]:5d}", max(0, w - 1 - x), attr_drag)
        x += len(f"{inc_k[1]:5d}")
        cui.addnstr(2, x, "/", max(0, w - 1 - x), attr_dim)
        x += 1
        cui.addnstr(2, x, f"{inc_k[2]:5d}", max(0, w - 1 - x), attr_hold)
        x += len(f"{inc_k[2]:5d}")
        cui.addnstr(2, x, "/", max(0, w - 1 - x), attr_dim)
        x += 1
        cui.addnstr(2, x, f"{inc_k[3]:5d}", max(0, w - 1 - x), attr_flick)
        x += len(f"{inc_k[3]:5d}")
        cui.addnstr(2, x, ")", max(0, w - 1 - x), attr_dim)
        x += 1

        if x + 2 < int(w):
            cui.addnstr(2, x, extra, max(0, w - 1 - x), attr_dim)
    except:
        pass


def _render_recorder_stats(cui: Any, w: int, speed: float, args: Any, attr_ok: int, attr_warn: int, attr_bad: int, attr_dim: int):
    """Render recorder statistics if available."""
    try:
        rec = getattr(args, "recorder", None)
        st = rec.get_stats() if (rec is not None and hasattr(rec, "get_stats")) else None
        if isinstance(st, dict) and st:
            out_size = st.get("out_size_bytes", None)
            out_mbps = st.get("out_mbps", None)
            avg_ms = float(st.get("avg_write_ms", 0.0) or 0.0)
            max_ms = float(st.get("max_write_ms", 0.0) or 0.0)
            slow = int(st.get("slow_write_calls", 0) or 0)
            in_mbps = float(st.get("mbps_in", 0.0) or 0.0)
            fps_w = float(st.get("fps_wall", 0.0) or 0.0)
            has_audio = bool(st.get("has_audio", False))
            codec_s = str(st.get("codec", ""))
            preset_s = str(st.get("preset", ""))

            def _fmt_size(b):
                if b is None:
                    return "?"
                if b < 1024:
                    return f"{int(b)}B"
                if b < 1024 * 1024:
                    return f"{float(b)/1024.0:.1f}KiB"
                if b < 1024 * 1024 * 1024:
                    return f"{float(b)/(1024.0*1024.0):.2f}MiB"
                return f"{float(b)/(1024.0*1024.0*1024.0):.2f}GiB"

            warn_attr = attr_ok
            if float(speed) < 1.0 or float(avg_ms) >= 20.0 or float(max_ms) >= 80.0:
                warn_attr = attr_warn
            if float(speed) < 0.7 or float(avg_ms) >= 40.0 or float(max_ms) >= 150.0:
                warn_attr = attr_bad

            row_stats = 3
            s_rec = f"enc: fps={fps_w:6.1f}  in={in_mbps:6.2f}MiB/s  write={avg_ms:5.1f}ms avg  {max_ms:5.1f}ms max  slow={slow:4d}  audio={'on' if has_audio else 'off'}"
            try:
                cui.addnstr(row_stats, 0, s_rec, max(0, w - 1), warn_attr)
            except:
                pass

            if out_size is not None:
                if out_mbps is None:
                    s_out = f"out: size={_fmt_size(out_size)}  rate=?MiB/s  codec={codec_s} preset={preset_s}"
                else:
                    s_out = f"out: size={_fmt_size(out_size)}  rate={float(out_mbps):5.2f}MiB/s  codec={codec_s} preset={preset_s}"
            else:
                s_out = f"out: size=?  rate=?  codec={codec_s} preset={preset_s}"
            try:
                cui.addnstr(row_stats + 1, 0, s_out, max(0, w - 1), attr_dim)
            except:
                pass
    except:
        pass


def _render_help_view(cui: Any, h: int, w: int):
    """Render the help view."""
    row = 3
    help_lines = [
        "Help:",
        "  q: quit recording",
        "  h: toggle this help",
        "  j/k or Up/Down: select line",
        "  PgUp/PgDn: select line faster",
        "  g/G or Home/End: top/bottom",
        "  +/-: adjust CUI refresh rate",
        "",
        "P=Past (already hit time passed), I=Incoming (t..t+approach) by kind: Tap/Drag/Hold/Flick",
    ]
    for ln in help_lines:
        if row >= h:
            break
        try:
            cui.addnstr(row, 0, ln, max(0, w - 1))
        except:
            pass
        row += 1


def _safe_addnstr(cui: Any, y: int, x: int, s: str, w: int, attr: int = 0):
    try:
        if int(w) <= 0:
            return
        cui.addnstr(int(y), int(x), str(s), int(w), int(attr))
    except:
        pass


def _draw_box(cui: Any, y0: int, x0: int, hh: int, ww: int, title: str, attr: int):
    if hh < 2 or ww < 2:
        return
    top = "+" + ("-" * max(0, ww - 2)) + "+"
    mid = "|" + (" " * max(0, ww - 2)) + "|"
    bot = "+" + ("-" * max(0, ww - 2)) + "+"
    _safe_addnstr(cui, y0, x0, top, ww, attr)
    for r in range(1, max(1, hh - 1)):
        _safe_addnstr(cui, y0 + r, x0, mid, ww, 0)
    _safe_addnstr(cui, y0 + hh - 1, x0, bot, ww, attr)
    if title:
        tt = f" {title} "
        _safe_addnstr(cui, y0, x0 + 1, tt, max(0, ww - 2), attr)


def _render_box_lines(
    cui: Any,
    y0: int,
    x0: int,
    hh: int,
    ww: int,
    title: str,
    lines: List[str],
    attr_title: int,
    attr_dim: int,
):
    _draw_box(cui, y0, x0, hh, ww, title, attr_title)
    if hh <= 2 or ww <= 2:
        return
    max_rows = max(0, hh - 2)
    max_w = max(0, ww - 2)
    for i in range(min(max_rows, len(lines))):
        _safe_addnstr(cui, y0 + 1 + i, x0 + 1, lines[i], max_w, attr_dim)


def _render_dashboard_view(
    cui: Any,
    h: int,
    w: int,
    cui_scroll: int,
    t: float,
    approach: float,
    note_times_by_line_kind: Dict[int, Dict[int, List[float]]],
    attr_dim: int,
    attr_head: int,
    events_incoming: Optional[List[str]],
    events_past: Optional[List[str]],
    lines: Optional[List[Any]],
    notes: Optional[List[Any]],
    states: Optional[List[Any]],
    idx_next: Optional[int],
    W: Optional[int],
    H: Optional[int],
) -> int:
    top = 5
    if int(h) <= int(top) + 2 or int(w) <= 10:
        return cui_scroll

    avail_h = int(h) - int(top) - 1
    avail_h = max(1, avail_h)

    left_w = max(10, int(w) // 2)
    right_w = max(10, int(w) - int(left_w) - 1)
    left_x = 0
    right_x = int(left_w) + 1

    left_top_h = max(3, avail_h // 2)
    left_bot_h = max(3, avail_h - left_top_h)

    inc = list(events_incoming or [])
    past = list(events_past or [])

    inc_show = inc[: max(0, left_top_h - 2)]
    past_show = past[: max(0, left_bot_h - 2)]

    _render_box_lines(cui, top, left_x, left_top_h, left_w, "incoming events", inc_show, attr_head, attr_dim)
    _render_box_lines(cui, top + left_top_h, left_x, left_bot_h, left_w, "past events", past_show, attr_head, attr_dim)

    line_box_h = min(12, max(6, avail_h // 3))
    note_box_h = max(3, avail_h - line_box_h)

    line_lines: List[str] = []
    lids = sorted(note_times_by_line_kind.keys())
    sel_idx = 0
    if lids:
        if int(cui_scroll) < 0:
            cui_scroll = 0
        if int(cui_scroll) > max(0, len(lids) - 1):
            cui_scroll = max(0, len(lids) - 1)
        sel_idx = int(cui_scroll)
        lid = int(lids[sel_idx])

        past4, inc4 = line_note_counts_kind(note_times_by_line_kind, int(lid), float(t), float(approach))
        line_lines.append(f"selected line: L{lid:02d}  idx={sel_idx+1}/{max(1, len(lids))}")
        line_lines.append(f"P {past4[0]}/{past4[1]}/{past4[2]}/{past4[3]}   I {inc4[0]}/{inc4[1]}/{inc4[2]}/{inc4[3]}")
        if lines is not None and int(lid) >= 0 and int(lid) < len(lines):
            try:
                lx, ly, lr, la01, lsc, laraw = eval_line_state(lines[int(lid)], float(t))
                line_lines.append(f"pos=({lx:7.1f},{ly:7.1f})  rot={lr:+7.3f}  alpha01={la01:4.2f} raw={laraw:+6.3f}")
                line_lines.append(f"scroll={lsc:10.2f}")
                try:
                    tr_rot = track_seg_state(getattr(lines[int(lid)], 'rot', None))
                    tr_alp = track_seg_state(getattr(lines[int(lid)], 'alpha', None))
                    tr_scr = track_seg_state(getattr(lines[int(lid)], 'scroll_px', None))
                    line_lines.append(f"seg rot={tr_rot}  a={tr_alp}  scr={tr_scr}")
                except:
                    pass
            except:
                line_lines.append("(line state unavailable)")
    else:
        line_lines.append("no lines")

    _render_box_lines(cui, top, right_x, line_box_h, right_w, "line properties", line_lines[: max(0, line_box_h - 2)], attr_head, attr_dim)

    note_lines: List[str] = []
    if notes is not None and states is not None and lines is not None and W is not None and H is not None:
        try:
            W0 = int(W)
            H0 = int(H)
            margin = 120
            base_note_w = max(1, int(0.06 * float(W0)))
            base_note_h = max(1, int(0.018 * float(H0)))

            if idx_next is None:
                start_i = 0
            else:
                start_i = max(0, int(idx_next) - 64)
            end_i = min(len(notes), start_i + 512)

            shown = 0
            for i in range(start_i, end_i):
                if shown >= max(1, note_box_h - 2):
                    break
                n = notes[i]
                s = states[i] if i < len(states) else None

                try:
                    t_enter = float(getattr(n, 't_enter', -1e9))
                except:
                    t_enter = -1e9
                if float(t) < float(t_enter):
                    continue

                try:
                    lid = int(getattr(n, 'line_id', 0))
                except:
                    lid = 0
                if lid < 0 or lid >= len(lines):
                    continue

                try:
                    lx, ly, lr, la01, lsc, laraw = eval_line_state(lines[lid], float(t))
                except:
                    continue

                kind = int(getattr(n, 'kind', 0) or 0)
                above = bool(getattr(n, 'above', True))
                nid = int(getattr(n, 'nid', i))
                hit = bool(getattr(s, 'hit', False)) if s is not None else False
                holding = bool(getattr(s, 'holding', False)) if s is not None else False
                miss = bool(getattr(s, 'miss', False)) if s is not None else False

                if kind == 3:
                    try:
                        sh = float(getattr(n, 'scroll_hit', 0.0))
                        se = float(getattr(n, 'scroll_end', 0.0))
                    except:
                        continue
                    if hit or holding or (float(t) >= float(getattr(n, 't_hit', 0.0))):
                        head_target_scroll = sh if float(lsc) <= float(sh) else float(lsc)
                    else:
                        head_target_scroll = float(sh)
                    hx, hy = note_world_pos(float(lx), float(ly), float(lr), float(lsc), n, float(head_target_scroll), False)
                    tx, ty = note_world_pos(float(lx), float(ly), float(lr), float(lsc), n, float(se), True)
                    minx = min(float(hx), float(tx))
                    maxx = max(float(hx), float(tx))
                    miny = min(float(hy), float(ty))
                    maxy = max(float(hy), float(ty))
                    if maxx < -margin or minx > float(W0 + margin) or maxy < -margin or miny > float(H0 + margin):
                        continue
                    flg = ("H" if hit else "-") + ("h" if holding else "-") + ("M" if miss else "-")
                    note_lines.append(f"#{i:05d} nid={nid:6d} HOLD L{lid:02d} {'A' if above else 'B'} {flg} head=({hx:7.1f},{hy:7.1f}) tail=({tx:7.1f},{ty:7.1f})")
                    shown += 1
                else:
                    try:
                        sh = float(getattr(n, 'scroll_hit', 0.0))
                    except:
                        continue
                    x, y = note_world_pos(float(lx), float(ly), float(lr), float(lsc), n, float(sh), False)
                    ws = float(base_note_w) * float(getattr(n, 'size_px', 1.0) or 1.0)
                    hs = float(base_note_h) * float(getattr(n, 'size_px', 1.0) or 1.0)
                    if (float(x) + ws / 2 < -margin) or (float(x) - ws / 2 > float(W0 + margin)) or (float(y) + hs / 2 < -margin) or (float(y) - hs / 2 > float(H0 + margin)):
                        continue
                    kd = {1: 'TAP', 2: 'DRG', 4: 'FLK'}.get(kind, str(kind))
                    flg = ("H" if hit else "-") + ("h" if holding else "-") + ("M" if miss else "-")
                    note_lines.append(f"#{i:05d} nid={nid:6d} {kd:3s}  L{lid:02d} {'A' if above else 'B'} {flg} pos=({x:7.1f},{y:7.1f})")
                    shown += 1
        except:
            note_lines.append("(notes unavailable)")
    else:
        note_lines.append("(notes unavailable)")

    _render_box_lines(cui, top + line_box_h, right_x, note_box_h, right_w, "on-screen notes", note_lines[: max(0, note_box_h - 2)], attr_head, attr_dim)

    try:
        footer = f"view=dashboard  line={sel_idx+1}/{max(1, len(lids))}  refresh={float(record_curses_fps):.1f}Hz"
        _safe_addnstr(cui, int(h) - 1, 0, footer, max(0, int(w) - 1), attr_dim)
    except:
        pass

    return cui_scroll
