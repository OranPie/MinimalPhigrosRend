from __future__ import annotations

import bisect
from typing import Any, Dict, List, Optional

from ...math.util import clamp, now_sec
from ...core.ui import compute_score
from .rendering_helpers import line_note_counts_kind


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

        # Help or line details view
        if int(cui_view) != 0:
            _render_help_view(cui, h, w)
        else:
            cui_scroll = _render_lines_view(
                cui, h, w, cui_scroll, t, approach,
                note_times_by_line_kind, attr_dim, record_curses_fps
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
        "  j/k or Up/Down: scroll lines",
        "  PgUp/PgDn: scroll faster",
        "  g/G or Home/End: top/bottom",
        "  +/-: adjust CUI refresh rate",
        "",
        "Columns: P=Past (already hit time passed), I=Incoming (t..t+approach) by kind: Tap/Drag/Hold/Flick",
    ]
    for ln in help_lines:
        if row >= h:
            break
        try:
            cui.addnstr(row, 0, ln, max(0, w - 1))
        except:
            pass
        row += 1


def _render_lines_view(
    cui: Any, h: int, w: int, cui_scroll: int, t: float, approach: float,
    note_times_by_line_kind: Dict[int, Dict[int, List[float]]],
    attr_dim: int, record_curses_fps: float
) -> int:
    """Render the lines view. Returns updated cui_scroll."""
    row = 3
    max_lines = max(0, h - row - 1)
    lids = sorted(note_times_by_line_kind.keys())
    if int(cui_scroll) < 0:
        cui_scroll = 0
    if int(cui_scroll) > max(0, len(lids) - 1):
        cui_scroll = max(0, len(lids) - 1)
    start = int(cui_scroll)
    shown = 0

    hdr = "Line      Past(T/D/H/F)                 Incoming(T/D/H/F)"
    try:
        cui.addnstr(row, 0, hdr, max(0, w - 1))
    except:
        pass
    row += 1

    for lid in lids[start:]:
        if shown >= max_lines:
            break
        past4, inc4 = line_note_counts_kind(note_times_by_line_kind, int(lid), float(t), approach)
        s = f"L{int(lid):02d}   {past4[0]:5d}/{past4[1]:5d}/{past4[2]:5d}/{past4[3]:5d}        {inc4[0]:5d}/{inc4[1]:5d}/{inc4[2]:5d}/{inc4[3]:5d}"
        try:
            cui.addnstr(row, 0, s, max(0, w - 1))
        except:
            pass
        row += 1
        shown += 1

    try:
        cui_view_str = 'help' if False else 'lines'
        footer = f"lines {start+1}/{max(1, len(lids))}  view={cui_view_str}  refresh={float(record_curses_fps):.1f}Hz"
        cui.addnstr(h - 1, 0, footer, max(0, w - 1))
    except:
        pass

    return cui_scroll
