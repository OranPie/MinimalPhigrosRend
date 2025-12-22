from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

import pygame

from ...core.ui import compute_score, format_title, progress_ratio


def render_ui_overlay(
    screen: pygame.Surface,
    *,
    font: pygame.font.Font,
    small: pygame.font.Font,
    W: int,
    H: int,
    t: float,
    chart_end: float,
    chart_info: Dict[str, Any],
    judge: Any,
    total_notes: int,
    idx_next: int,
    states_len: int,
    lines_len: int,
    fmt: str,
    expand: float,
    particles_count: int,
    note_render_count: int,
    hit_debug: bool,
    hit_debug_lines: Any,
    advance_active: bool,
    start_time: Optional[float],
    args: Any,
    clock: pygame.time.Clock,
):
    """Render the UI overlay (score, combo, debug info, etc.)."""
    ui_pad = max(4, int(small.get_linesize() * 0.25))
    ui_x = 16
    ui_y0 = 14
    ui_combo_y = ui_y0
    ui_score_y = ui_combo_y + font.get_linesize() + ui_pad
    ui_fmt_y = ui_score_y + small.get_linesize() + max(2, ui_pad // 2)
    ui_particles_y = ui_fmt_y + small.get_linesize() + max(2, ui_pad // 2)
    ui_hitdbg_y = ui_fmt_y + small.get_linesize() + ui_pad

    if getattr(args, "debug_particles", False):
        txt = small.render(f"particles={particles_count}", True, (220, 220, 220))
        screen.blit(txt, (ui_x, ui_particles_y))

    if chart_end > 1e-6:
        pbar = progress_ratio(t, chart_end, advance_active=advance_active, start_time=start_time)
        pygame.draw.rect(screen, (40, 40, 40), pygame.Rect(0, 0, W, 6))
        pygame.draw.rect(screen, (230, 230, 230), pygame.Rect(0, 0, int(W * pbar), 6))

    combo_txt = font.render(f"COMBO {judge.combo}", True, (240, 240, 240))
    screen.blit(combo_txt, (ui_x, ui_combo_y))

    score, acc_ratio, _combo_ratio = compute_score(judge.acc_sum, judge.judged_cnt, judge.combo, judge.max_combo, total_notes)
    score_txt = small.render(
        f"SCORE {score:07d}   HIT {acc_ratio*100:6.2f}%   MAX {judge.max_combo}/{total_notes}",
        True,
        (200, 200, 200),
    )
    screen.blit(score_txt, (ui_x, ui_score_y))

    extra_lines: List[str] = []
    try:
        if bool(getattr(args, "advance_seq_overlay", False)) and bool(advance_active):
            ci = chart_info if isinstance(chart_info, dict) else {}
            st = ci.get("seg_start_time", None)
            en = ci.get("seg_end_time", None)
            si = ci.get("seg_index", None)
            ss = ci.get("seg_total", None)
            if st is not None and en is not None:
                song_t = float(t) - float(st)
                dur = max(1e-6, float(en) - float(st))
                song_p = clamp(song_t / dur, 0.0, 1.0)
                if si is not None and ss is not None:
                    extra_lines.append(f"seq={int(si)}/{int(ss)}  song_t={song_t:7.3f}s  song={song_p*100:6.2f}%")
                else:
                    extra_lines.append(f"song_t={song_t:7.3f}s  song={song_p*100:6.2f}%")
            elif si is not None and ss is not None:
                extra_lines.append(f"seq={int(si)}/{int(ss)}")
    except Exception:
        pass

    fmt_txt = small.render(f"fmt={fmt}  t={t:7.3f}s  next={idx_next}/{states_len}  lines={lines_len}", True, (180, 180, 180))
    screen.blit(fmt_txt, (ui_x, ui_fmt_y))
    if extra_lines:
        for j, s in enumerate(extra_lines, start=1):
            txt = small.render(str(s), True, (180, 180, 180))
            screen.blit(txt, (ui_x, ui_fmt_y + j * small.get_linesize()))

    if hit_debug and hit_debug_lines:
        cols = max(1, int(getattr(args, "hit_debug_cols", 5) or 5))
        shown = 0
        for rec in list(hit_debug_lines)[:cols]:
            try:
                dt_ms = float(rec.get("dt_ms", 0.0))
                nid = int(rec.get("nid", -1))
                jd = str(rec.get("judgement", ""))
                hp = rec.get("hold_percent", None)
                hp_s = "-" if hp is None else f"{float(hp)*100:5.1f}%"
                s = f"{dt_ms:+7.1f}ms  id={nid:6d}  {jd:7s}  hold={hp_s}"
                txt = small.render(s, True, (200, 200, 200))
                screen.blit(txt, (ui_x, ui_hitdbg_y + shown * small.get_linesize()))
                shown += 1
            except:
                pass

    if chart_info and (not getattr(args, "no_title_overlay", False)):
        title, sub = format_title(chart_info)
        t1 = small.render(title, True, (230, 230, 230))
        t2 = small.render(sub, True, (180, 180, 180))
        screen.blit(t1, (W - 16 - t1.get_width(), 14))
        if sub:
            screen.blit(t2, (W - 16 - t2.get_width(), 14 + small.get_linesize()))

    hint = small.render("LMB/SPACE: hit   hold: keep pressed   P: pause   R: restart   ESC: quit", True, (160, 160, 160))
    screen.blit(hint, (ui_x, H - small.get_linesize() - ui_pad))

    if getattr(args, "basic_debug", False):
        try:
            fps = float(clock.get_fps())
        except:
            fps = 0.0
        dbg = small.render(f"FPS {fps:6.1f}   NOTE_RENDER {int(note_render_count)}", True, (220, 220, 220))
        screen.blit(dbg, (ui_x, ui_particles_y + small.get_linesize() + ui_pad))
