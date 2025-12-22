from __future__ import annotations

from typing import Any, List, Tuple

import pygame

from ...core.ui import compute_score, progress_ratio
from ...core.fx import prune_particles
from .particles import draw_particles


def blit_line_text_draw_calls(
    *,
    target: pygame.Surface,
    line_text_draw_calls: List[Tuple[int, pygame.Surface, float, float]],
):
    if not line_text_draw_calls:
        return
    line_text_draw_calls.sort(key=lambda x: x[0])
    for _pr, surf, x0, y0 in line_text_draw_calls:
        target.blit(surf, (x0, y0))


def draw_expand_border(*, screen: pygame.Surface, W: int, H: int, expand: float):
    if float(expand) <= 1.0:
        return
    bw = float(W) / float(expand)
    bh = float(H) / float(expand)
    x0 = (float(W) - bw) * 0.5
    y0 = (float(H) - bh) * 0.5
    pygame.draw.rect(screen, (240, 240, 240), pygame.Rect(int(x0), int(y0), int(bw), int(bh)), 2)


def post_render_record_headless_overlay(
    *,
    args: Any,
    display_frame: pygame.Surface,
    W: int,
    H: int,
    t: float,
    chart_end: float,
    fmt: str,
    idx_next: int,
    states_len: int,
    lines_len: int,
    total_notes: int,
    judge: Any,
    particles: List[Any],
    line_text_draw_calls: List[Tuple[int, pygame.Surface, float, float]],
    font: Any,
    small: Any,
    expand: float,
    advance_active: bool,
    hit_debug: bool,
    hit_debug_lines: Any,
    start_time: Any = None,
):
    if getattr(args, "record_render_particles", False):
        try:
            now_ms = int(float(t) * 1000.0)
            particles[:] = prune_particles(particles, now_ms)
            draw_particles(display_frame, particles, now_ms, int(W), int(H), float(expand))
        except Exception:
            pass

    if getattr(args, "record_render_text", False):
        try:
            blit_line_text_draw_calls(target=display_frame, line_text_draw_calls=line_text_draw_calls)
        except Exception:
            pass

        try:
            ui_pad = max(4, int(small.get_linesize() * 0.25))
            ui_x = 16
            ui_y0 = 14
            ui_combo_y = ui_y0
            ui_score_y = ui_combo_y + font.get_linesize() + ui_pad
            ui_fmt_y = ui_score_y + small.get_linesize() + max(2, ui_pad // 2)
            ui_particles_y = ui_fmt_y + small.get_linesize() + max(2, ui_pad // 2)
            ui_hitdbg_y = ui_fmt_y + small.get_linesize() + ui_pad

            if getattr(args, "debug_particles", False):
                txt = small.render(f"particles={len(particles)}", True, (220, 220, 220))
                display_frame.blit(txt, (ui_x, ui_particles_y))

            if float(chart_end) > 1e-6:
                st = start_time if start_time is not None else getattr(args, "start_time", None)
                pbar = progress_ratio(float(t), float(chart_end), advance_active=bool(advance_active), start_time=st)
                pygame.draw.rect(display_frame, (40, 40, 40), pygame.Rect(0, 0, int(W), 6))
                pygame.draw.rect(display_frame, (230, 230, 230), pygame.Rect(0, 0, int(int(W) * float(pbar)), 6))

            combo_txt = font.render(f"COMBO {judge.combo}", True, (240, 240, 240))
            display_frame.blit(combo_txt, (ui_x, ui_combo_y))

            score, acc_ratio, _combo_ratio = compute_score(judge.acc_sum, judge.judged_cnt, judge.combo, judge.max_combo, int(total_notes))
            score_txt = small.render(
                f"SCORE {score:07d}   HIT {acc_ratio*100:6.2f}%   MAX {judge.max_combo}/{int(total_notes)}",
                True,
                (200, 200, 200),
            )
            display_frame.blit(score_txt, (ui_x, ui_score_y))

            fmt_txt = small.render(
                f"fmt={str(fmt)}  t={float(t):7.3f}s  next={int(idx_next)}/{int(states_len)}  lines={int(lines_len)}",
                True,
                (180, 180, 180),
            )
            display_frame.blit(fmt_txt, (ui_x, ui_fmt_y))

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
                        display_frame.blit(txt, (ui_x, ui_hitdbg_y + shown * small.get_linesize()))
                        shown += 1
                    except Exception:
                        pass
        except Exception:
            pass


def post_render_non_headless(
    *,
    screen: pygame.Surface,
    W: int,
    H: int,
    expand: float,
    t: float,
    particles: List[Any],
    line_text_draw_calls: List[Tuple[int, pygame.Surface, float, float]],
):
    draw_expand_border(screen=screen, W=int(W), H=int(H), expand=float(expand))

    now_ms = int(float(t) * 1000.0)
    particles[:] = prune_particles(particles, now_ms)
    draw_particles(screen, particles, now_ms, int(W), int(H), float(expand))

    blit_line_text_draw_calls(target=screen, line_text_draw_calls=line_text_draw_calls)
