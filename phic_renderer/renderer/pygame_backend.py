from __future__ import annotations

import bisect
import logging
import math
import os
import random
import time
from collections import deque
from typing import Any, Dict, List, Optional, Tuple

import pygame
import numpy as np

from .. import state
from ..io.chart_loader_impl import load_chart
from ..io.chart_pack_impl import load_chart_pack
from ..runtime.effects import HitFX, ParticleBurst
from ..core.fx import prune_hitfx, prune_particles
from ..runtime.judge import Judge, JUDGE_WEIGHT
from ..runtime.kinematics import eval_line_state, note_world_pos
from ..runtime.judge_script import build_judge_plan, load_judge_script, parse_judge_script
from ..core.constants import NOTE_TYPE_COLORS
from ..core.ui import compute_score, format_title, progress_ratio
from ..math.util import (
    apply_expand_xy,
    apply_expand_pts,
    clamp,
    now_sec,
    rect_corners,
)
from ..runtime.visibility import precompute_t_enter
from ..types import NoteState, RuntimeLine, RuntimeNote
from ..audio import create_audio_backend
from .pygame.draw import draw_line_rgba, draw_poly_outline_rgba, draw_poly_rgba, draw_ring
from .pygame.hold import draw_hold_3slice
from .pygame.respack_loader import load_respack
from .pygame.fonts import load_fonts
from .pygame.background import load_background
from .pygame.particles import draw_particles
from .pygame.hitsound import HitsoundPlayer
from .pygame.hitfx import draw_hitfx
from .pygame.transform_cache import get_global_transform_cache
from .pygame.texture_atlas import get_global_atlas, get_global_texture_map, set_global_texture_map
from .pygame.batch_renderer import get_global_batch_renderer
from .pygame.surface_pool import get_global_pool

from .pygame.rendering_helpers import (
    pick_note_image,
    compute_note_times_by_line,
    compute_note_times_by_line_kind,
    line_note_counts,
    line_note_counts_kind,
    track_seg_state,
    scroll_speed_px_per_sec,
)
from .pygame.ui_rendering import render_ui_overlay
from .pygame.recording_utils import (
    print_recording_progress,
    print_recording_notes,
    init_curses_ui,
    cleanup_curses_ui,
    handle_curses_input,
)
from .pygame.init_helpers import (
    compute_total_notes,
    compute_chart_end,
    group_simultaneous_notes,
    filter_notes_by_time,
)
from .pygame.judge_helpers import (
    sanitize_grade,
    apply_grade,
    finalize_hold,
    check_hold_release,
    detect_miss,
)
from .pygame.curses_ui import render_curses_ui

def run(
    args: Any,
    *,
    W: int,
    H: int,
    expand: float,
    fmt: str,
    offset: float,
    lines: List[RuntimeLine],
    notes: List[RuntimeNote],
    chart_info: Dict[str, Any],
    bg_dim_alpha: Optional[int],
    bg_path: Optional[str],
    music_path: Optional[str],
    chart_path: Optional[str],
    advance_active: bool,
    advance_cfg: Optional[Dict[str, Any]],
    advance_mix: bool,
    advance_tracks_bgm: List[Dict[str, Any]],
    advance_main_bgm: Optional[str],
    advance_segment_starts: List[float],
    advance_segment_bgm: List[Optional[str]],
    advance_base_dir: Optional[str],
):
    logger = logging.getLogger(__name__)
    pygame.init()

    try:
        logger.info(
            "[pygame] start (fmt=%s, W=%s, H=%s, expand=%s, advance=%s, record=%s)",
            fmt,
            int(W),
            int(H),
            float(expand),
            bool(advance_active),
            bool(getattr(args, "record_enabled", False) or getattr(args, "record_dir", None) or getattr(args, "recorder", None)),
        )
    except Exception:
        pass

    _signal = None
    _old_sigint = None
    try:
        import signal as _sig
        _signal = _sig
        try:
            _old_sigint = _signal.getsignal(_signal.SIGINT)
        except:
            _old_sigint = None

        def _on_sigint(_signum, _frame):
            try:
                setattr(state, "_sigint", True)
            except:
                pass

        try:
            _signal.signal(_signal.SIGINT, _on_sigint)
        except:
            pass
    except:
        _signal = None
        _old_sigint = None

    clock = pygame.time.Clock()

    record_dir = getattr(args, "record_dir", None)
    record_fps = float(getattr(args, "record_fps", 60.0) or 60.0)
    record_start_time = float(getattr(args, "record_start_time", 0.0) or 0.0)
    record_end_time = getattr(args, "record_end_time", None)
    record_end_time = float(record_end_time) if record_end_time is not None else None
    recorder = getattr(args, "recorder", None)
    record_enabled = bool(getattr(args, "record_enabled", False)) or bool(record_dir) or bool(recorder)
    record_headless = bool(getattr(args, "record_headless", False)) and record_enabled
    record_log_interval = float(getattr(args, "record_log_interval", 1.0) or 0.0) if record_enabled else 0.0
    record_log_notes = bool(getattr(args, "record_log_notes", False)) if record_enabled else False
    record_use_curses = bool(getattr(args, "record_use_curses", False)) and record_headless
    record_curses_fps = float(getattr(args, "record_curses_fps", 10.0) or 10.0)
    if record_curses_fps <= 1e-6:
        record_curses_fps = 10.0
    record_render_particles = bool(getattr(args, "record_render_particles", True))
    record_render_text = bool(getattr(args, "record_render_text", True))
    record_preview_audio = bool(getattr(args, "record_preview_audio", False))

    logger.debug(
        "[pygame] record settings enabled=%s headless=%s fps=%s start=%s end=%s log_interval=%s log_notes=%s use_curses=%s",
        record_enabled,
        record_headless,
        record_fps,
        record_start_time,
        record_end_time,
        record_log_interval,
        record_log_notes,
        record_use_curses,
    )

    audio = create_audio_backend(getattr(args, "audio_backend", "pygame"))

    try:
        logger.info("[pygame] audio backend: %s", str(getattr(args, "audio_backend", "pygame")))
    except Exception:
        pass

    respack = load_respack(str(args.respack), audio=audio) if getattr(args, "respack", None) else None
    state.respack = respack

    if getattr(args, "respack", None):
        logger.info("[pygame] respack: %s", str(getattr(args, "respack")))
    else:
        logger.debug("[pygame] respack: (none)")

    # Background/BGM resolution
    bg_file = getattr(args, "bg", None) if getattr(args, "bg", None) else (bg_path if (bg_path and os.path.exists(bg_path)) else None)
    # Default: if chart pack provides music, prefer it over config/CLI bgm.
    # Use --force to force config/CLI bgm even when pack music exists.
    if bool(getattr(args, "force", False)):
        bgm_file = getattr(args, "bgm", None) if getattr(args, "bgm", None) else (music_path if (music_path and os.path.exists(music_path)) else None)
    else:
        bgm_file = (music_path if (music_path and os.path.exists(music_path)) else None) if (music_path and os.path.exists(music_path)) else None
        if not bgm_file:
            bgm_file = getattr(args, "bgm", None)

    logger.info(
        "[pygame] resolved assets bg=%s bgm=%s (force=%s advance=%s)",
        str(bg_file) if bg_file else "(none)",
        str(bgm_file) if bgm_file else "(none)",
        bool(getattr(args, "force", False)),
        bool(advance_active),
    )

    if advance_active:
        base_dir = advance_base_dir or os.getcwd()
        if getattr(args, "bg", None):
            bg_file = args.bg
        else:
            bg_file = bg_path if (bg_path and os.path.exists(bg_path)) else None
        if bg_file and (not os.path.isabs(str(bg_file))):
            cand = os.path.join(base_dir, str(bg_file))
            if os.path.exists(cand):
                bg_file = cand

        if bool(getattr(args, "force", False)) and getattr(args, "bgm", None):
            bgm_file = args.bgm
        else:
            bgm_file = advance_main_bgm
        if bgm_file and (not os.path.isabs(str(bgm_file))):
            cand = os.path.join(base_dir, str(bgm_file))
            if os.path.exists(cand):
                bgm_file = cand

        logger.debug(
            "[pygame] advance base_dir=%s bg=%s bgm=%s",
            str(base_dir),
            str(bg_file) if bg_file else "(none)",
            str(bgm_file) if bgm_file else "(none)",
        )

    bg_base, bg_blurred = load_background(bg_file, W, H, int(getattr(args, "bg_blur", 10)))

    logger.info(
        "[pygame] background loaded (file=%s, blur=%s)",
        str(bg_file) if bg_file else "(none)",
        int(getattr(args, "bg_blur", 10)),
    )

    # BGM
    use_bgm_clock = False
    advance_mix_failed = False
    advance_bgm_active = False
    advance_segment_idx = 0
    advance_sound_tracks: List[Dict[str, Any]] = []

    chart_speed = float(getattr(args, "chart_speed", 1.0) or 1.0)
    if chart_speed <= 1e-9:
        chart_speed = 1.0
    start_time_sec = 0.0
    end_time_sec = None
    if (not advance_active) and getattr(args, "start_time", None) is not None:
        try:
            start_time_sec = float(getattr(args, "start_time"))
        except:
            start_time_sec = 0.0
    if (not advance_active) and getattr(args, "end_time", None) is not None:
        try:
            end_time_sec = float(getattr(args, "end_time"))
        except:
            end_time_sec = None

    music_start_pos_sec = float(offset) + float(start_time_sec) / float(chart_speed)
    if music_start_pos_sec < 0.0:
        music_start_pos_sec = 0.0

    if not advance_active:
        if (not record_enabled) and bgm_file:
            audio.play_music_file(
                str(bgm_file),
                volume=clamp(getattr(args, "bgm_volume", 0.8), 0.0, 1.0),
                start_pos_sec=float(music_start_pos_sec),
            )
            use_bgm_clock = True
            logger.info(
                "[pygame] bgm play (file=%s, volume=%s, start_pos=%s)",
                str(bgm_file),
                clamp(getattr(args, "bgm_volume", 0.8), 0.0, 1.0),
                float(music_start_pos_sec),
            )
        elif record_enabled and bgm_file:
            logger.info("[pygame] bgm suppressed due to recording (file=%s)", str(bgm_file))
    else:
        use_bgm_clock = False
        if advance_mix:
            try:
                if advance_tracks_bgm:
                    for tr in advance_tracks_bgm:
                        pth = str(tr.get("path"))
                        if pth and os.path.exists(pth):
                            snd = audio.load_sound(pth)
                            advance_sound_tracks.append({
                                "start_at": float(tr.get("start_at", 0.0)),
                                "end_at": tr.get("end_at", None),
                                "sound": snd,
                                "channel": None,
                                "started": False,
                                "stopped": False,
                            })
                elif advance_segment_bgm:
                    for i, pth in enumerate(advance_segment_bgm):
                        if pth and os.path.exists(str(pth)):
                            snd = audio.load_sound(str(pth))
                            advance_sound_tracks.append({
                                "start_at": float(advance_segment_starts[i]) if i < len(advance_segment_starts) else 0.0,
                                "end_at": None,
                                "sound": snd,
                                "channel": None,
                                "started": False,
                                "stopped": False,
                            })
                if advance_sound_tracks and advance_segment_starts:
                    adv_sorted = sorted(range(len(advance_sound_tracks)), key=lambda k: float(advance_sound_tracks[k]["start_at"]))
                    for j in range(len(adv_sorted) - 1):
                        cur = advance_sound_tracks[adv_sorted[j]]
                        nxt = advance_sound_tracks[adv_sorted[j + 1]]
                        cur["end_at"] = float(nxt["start_at"])
                logger.info("[pygame] advance mix: prepared %s tracks", len(advance_sound_tracks))
            except Exception:
                advance_mix_failed = True
                advance_sound_tracks = []
                logger.exception("[pygame] advance mix: failed, will fallback")

        if (not advance_mix) or advance_mix_failed:
            try:
                if advance_segment_bgm:
                    if advance_segment_bgm[0] and os.path.exists(str(advance_segment_bgm[0])):
                        if not record_enabled:
                            audio.play_music_file(str(advance_segment_bgm[0]), volume=clamp(getattr(args, "bgm_volume", 0.8), 0.0, 1.0))
                        advance_bgm_active = True
                        advance_segment_idx = 0
                        logger.info("[pygame] advance bgm: using segment[0]=%s", str(advance_segment_bgm[0]))
                elif bgm_file and os.path.exists(str(bgm_file)):
                    if not record_enabled:
                        audio.play_music_file(str(bgm_file), volume=clamp(getattr(args, "bgm_volume", 0.8), 0.0, 1.0))
                    advance_bgm_active = True
                    logger.info("[pygame] advance bgm: using bgm_file=%s", str(bgm_file))
            except Exception:
                pass

    if getattr(args, "multicolor_lines", False):
        for ln in lines:
            if getattr(ln, "color", None) is not None:
                try:
                    rr, gg, bb = ln.color.eval(0)
                except:
                    rr, gg, bb = ln.color_rgb
            else:
                rr, gg, bb = ln.color_rgb
            ln.color_rgb = (int(rr), int(gg), int(bb))
    else:
        for ln in lines:
            ln.color_rgb = (255, 255, 255)

    # Precompute first entry time for each note before creating a window (temporary).
    precompute_t_enter(lines, notes, W, H)

    if record_headless:
        screen = pygame.Surface((W, H), pygame.SRCALPHA)
    else:
        screen = pygame.display.set_mode((W, H))
        pygame.display.set_caption("Mini Phigros Renderer (Official + RPE, rot/alpha/color)")
        if respack and getattr(respack, "img", None):
            try:
                for k, surf in list(respack.img.items()):
                    try:
                        respack.img[k] = surf.convert_alpha()
                    except:
                        respack.img[k] = surf
                try:
                    respack.hitfx_sheet = respack.img.get("hit_fx.png", respack.hitfx_sheet)
                except:
                    pass

                # Build texture atlas for performance optimization
                if not getattr(args, "disable_atlas", False):
                    try:
                        atlas = get_global_atlas()
                        texture_map = atlas.build_from_respack(respack)
                        set_global_texture_map(texture_map)
                        atlas_stats = atlas.get_stats()
                        if getattr(args, "basic_debug", False):
                            print(f"[Atlas] Built with {atlas_stats['texture_count']} textures, {atlas_stats['utilization']:.1f}% utilization")
                    except Exception as e:
                        if getattr(args, "basic_debug", False):
                            print(f"[Atlas] Failed to build: {e}")
            except:
                pass

    font, small = load_fonts(getattr(args, "font_path", None), float(getattr(args, "font_size_multiplier", 1.0) or 1.0))

    # chart directory for RPE hitsound relative paths
    chart_dir = os.path.dirname(os.path.abspath(chart_path)) if chart_path else ((advance_base_dir or os.getcwd()) if advance_active else os.getcwd())

    line_tex_cache: Dict[str, pygame.Surface] = {}

    last_debug_ms = 0

    # Apply start_time/end_time filtering for single charts
    if not advance_active:
        notes = filter_notes_by_time(notes, getattr(args, "start_time", None), getattr(args, "end_time", None))

    # Minimal simultaneous grouping
    group_simultaneous_notes(notes)

    # Total notes
    total_notes = compute_total_notes(notes, advance_active, advance_cfg, W, H)

    # chart end
    chart_end = compute_chart_end(notes, advance_active, getattr(args, "end_time", None) if (not advance_active) else None)

    note_times_by_line = compute_note_times_by_line(notes)
    note_times_by_line_kind, note_times_by_kind = compute_note_times_by_line_kind(notes)

    hold_fx_interval_ms = max(10, int(getattr(args, "hold_fx_interval_ms", 200)))
    hold_tail_tol = clamp(float(getattr(args, "hold_tail_tol", 0.8)), 0.0, 1.0)
    hitsound_min_interval_ms = max(0, int(getattr(args, "hitsound_min_interval_ms", 30)))

    hitsound = HitsoundPlayer(audio=audio, chart_dir=chart_dir, min_interval_ms=hitsound_min_interval_ms)

    # Note states
    states = [NoteState(n) for n in notes]
    idx_next = 0

    judge = Judge()
    hitfx: List[HitFX] = []
    particles: List[ParticleBurst] = []

    MISS_WINDOW = 0.160
    MISS_FADE_SEC = 0.35
    BAD_GHOST_SEC = 0.22
    bad_ghosts: List[Dict[str, Any]] = []

    judge_plan = None
    judge_plan_err = None
    judge_script_path = getattr(args, "judge_script", None)
    if judge_script_path:
        try:
            js_raw = load_judge_script(str(judge_script_path))
            js = parse_judge_script(js_raw)
            judge_plan = build_judge_plan(js, notes)
        except Exception as e:
            judge_plan = None
            judge_plan_err = str(e)

    def _sanitize_grade(note_kind: int, grade: Optional[str]) -> Optional[str]:
        if grade is None:
            return None
        g = str(grade).upper()
        k = int(note_kind)
        if k in (2, 4):
            return "PERFECT" if g == "PERFECT" else None
        if k == 3:
            if g == "PERFECT":
                return "PERFECT"
            if g in ("GOOD", "BAD"):
                return "GOOD"
            return None
        if k == 1:
            if g in ("PERFECT", "GOOD", "BAD"):
                return g
            return None
        return None

    def _apply_grade(s: NoteState, grade: str):
        g = str(grade).upper()
        if g == "PERFECT":
            judge.bump()
            s.judged = True
            s.hit = True
            judge.acc_sum += JUDGE_WEIGHT.get("PERFECT", 1.0)
            judge.judged_cnt += 1
            return
        if g == "GOOD":
            judge.bump()
            s.judged = True
            s.hit = True
            judge.acc_sum += JUDGE_WEIGHT.get("GOOD", 0.6)
            judge.judged_cnt += 1
            return
        if g == "BAD":
            judge.break_combo()
            s.judged = True
            s.hit = True
            judge.acc_sum += JUDGE_WEIGHT.get("BAD", 0.0)
            judge.judged_cnt += 1
            return

    hit_debug = bool(getattr(args, "hit_debug", False))
    hit_debug_lines: deque = deque(maxlen=64)
    hit_debug_seq = 0

    # timebase
    t0 = now_sec()
    if (not advance_active) and (not use_bgm_clock) and start_time_sec > 1e-9:
        t0 = now_sec() - float(music_start_pos_sec)
    paused = False
    pause_t = 0.0
    pause_frame = None
    record_frame_idx = 0
    record_frame: Optional[pygame.Surface] = None
    record_wall_t0 = now_sec()

    trail_dim_cache_key: Optional[Tuple[int, int, int]] = None
    trail_dim_cache: Optional[pygame.Surface] = None

    last_record_log_t = -1e9
    last_cui_update_t = -1e9
    cui_ok = False
    cui = None
    curses_mod = None
    cui_scroll = 0
    cui_view = 0
    cui_has_color = False

    # input
    holding_input = False
    key_down = False

    # pointer gesture tracking (for manual judgement)
    pointer_down = False
    pointer_down_pos: Optional[Tuple[float, float]] = None
    pointer_last_pos: Optional[Tuple[float, float]] = None
    pointer_down_t: float = 0.0
    pointer_moved_px: float = 0.0
    pointer_tap_fire = False
    pointer_flick_fire = False
    pointer_up_fire = False

    def input_down():
        return holding_input or key_down

    prev_down = False

    # sizes
    base_note_w = int(0.06 * W)
    base_note_h = int(0.018 * H)
    _note_scale_x_raw = float(getattr(args, "note_scale_x", 1.0))
    _note_scale_y_raw = float(getattr(args, "note_scale_y", 1.0))
    _ex = float(expand) if expand is not None else 1.0
    if _ex <= 1.000001:
        _ex = 1.0
    note_scale_x = _note_scale_x_raw / _ex
    note_scale_y = _note_scale_y_raw / _ex
    hold_body_w = int(float(base_note_w) * float(note_scale_x))

    outline_w = max(1, int(round(2.0 / float(expand))))
    line_w = max(1, int(round(4.0 / float(expand))))
    dot_r = max(1, int(round(4.0 / float(expand))))

    line_len = int(6.75 * W)

    # Initialize transform cache for performance optimization
    transform_cache = get_global_transform_cache()

    surface_pool = get_global_pool()
    bg_scaled_cache_key: Optional[Tuple[int, int, int]] = None
    bg_scaled_cache: Optional[pygame.Surface] = None
    dim_surf_cache_key: Optional[Tuple[int, int, int]] = None
    dim_surf_cache: Optional[pygame.Surface] = None

    running = True
    note_render_count_last = 0
    note_dbg_cache: Dict[str, pygame.Surface] = {}
    if record_use_curses:
        cui_ok, cui, curses_mod, cui_has_color = init_curses_ui()
        if not cui_ok:
            record_use_curses = False
            cui = None
    while running:
        # Clear per-frame transform cache
        transform_cache.next_frame()

        if bool(getattr(state, "_sigint", False)):
            running = False
            break

        if record_enabled and record_fps > 1e-6:
            _dt_frame = 1.0 / float(record_fps)
        else:
            _dt_frame = clock.tick(120) / 1000.0

        # schedule advance mixed sounds
        if (not record_enabled or record_preview_audio) and advance_active and advance_sound_tracks:
            now_t = (now_sec() - t0) * float(getattr(args, "chart_speed", 1.0))
            for tr in advance_sound_tracks:
                if tr.get("stopped"):
                    continue
                st_at = float(tr.get("start_at", 0.0))
                en_at = tr.get("end_at", None)
                if (not tr.get("started")) and now_t >= st_at:
                    try:
                        ch = audio.play_sound(tr["sound"], volume=clamp(getattr(args, "bgm_volume", 0.8), 0.0, 1.0))
                        tr["channel"] = ch
                        tr["started"] = True
                    except:
                        tr["started"] = True
                if en_at is not None and tr.get("started") and now_t >= float(en_at):
                    audio.stop_channel(tr.get("channel"))
                    tr["stopped"] = True

        if record_headless:
            pygame.event.pump()
            evs = []
        else:
            evs = pygame.event.get()

        if record_use_curses and cui_ok and cui is not None:
            try:
                should_quit, cui_view, cui_scroll, record_curses_fps = handle_curses_input(
                    cui,
                    curses_mod,
                    int(cui_view),
                    int(cui_scroll),
                    float(record_curses_fps),
                )
                if should_quit:
                    running = False
            except:
                pass
        for ev in evs:
            if ev.type == pygame.QUIT:
                running = False
            elif ev.type == pygame.KEYDOWN:
                if ev.key == pygame.K_ESCAPE:
                    running = False
                elif ev.key == pygame.K_SPACE:
                    key_down = True
                elif ev.key == pygame.K_p:
                    paused = not paused
                    if paused:
                        pause_t = now_sec()
                        try:
                            pause_frame = screen.copy()
                        except:
                            pause_frame = None
                        if use_bgm_clock:
                            audio.pause_music()
                    else:
                        if use_bgm_clock:
                            audio.unpause_music()
                        else:
                            t0 += now_sec() - pause_t
                        pause_frame = None
                elif ev.key == pygame.K_r:
                    if (not advance_active) and (not use_bgm_clock) and start_time_sec > 1e-9:
                        t0 = now_sec() - float(music_start_pos_sec)
                    else:
                        t0 = now_sec()
                    paused = False
                    if use_bgm_clock and bgm_file:
                        audio.stop_music()
                        audio.play_music_file(
                            str(bgm_file),
                            volume=clamp(getattr(args, "bgm_volume", 0.8), 0.0, 1.0),
                            start_pos_sec=float(music_start_pos_sec),
                        )
                    for s in states:
                        s.judged = s.hit = s.holding = s.released_early = s.miss = False
                    idx_next = 0
                    judge.combo = 0
                    hitfx.clear()
            elif ev.type == pygame.KEYUP:
                if ev.key == pygame.K_SPACE:
                    key_down = False
            elif ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1:
                holding_input = True
                pointer_down = True
                try:
                    pointer_down_pos = (float(ev.pos[0]), float(ev.pos[1]))
                    pointer_last_pos = (float(ev.pos[0]), float(ev.pos[1]))
                except Exception:
                    pointer_down_pos = None
                    pointer_last_pos = None
                pointer_down_t = float(t)
                pointer_moved_px = 0.0
            elif ev.type == pygame.MOUSEBUTTONUP and ev.button == 1:
                holding_input = False
                pointer_down = False
                pointer_up_fire = True
            elif ev.type == pygame.MOUSEMOTION:
                if holding_input or pointer_down:
                    try:
                        mx, my = float(ev.pos[0]), float(ev.pos[1])
                        if pointer_last_pos is not None:
                            dx = mx - float(pointer_last_pos[0])
                            dy = my - float(pointer_last_pos[1])
                            pointer_moved_px += float((dx * dx + dy * dy) ** 0.5)
                        pointer_last_pos = (mx, my)
                    except Exception:
                        pass

        if paused:
            if pause_frame is not None:
                screen.blit(pause_frame, (0, 0))
            else:
                screen.fill((10, 10, 15))
            txt = font.render("PAUSED (P to resume)", True, (220, 220, 220))
            screen.blit(txt, (W // 2 - txt.get_width() // 2, H // 2))
            pygame.display.flip()

            if record_headless:
                paused = False
            else:
                continue

        if record_enabled and record_fps > 1e-6:
            t = float(record_start_time) + float(record_frame_idx) / float(record_fps)
        elif use_bgm_clock:
            audio_t = audio.music_pos_sec() or 0.0
            t = (audio_t - offset) * float(chart_speed)
        else:
            t = ((now_sec() - t0) - offset) * float(chart_speed)

        if record_enabled:
            if record_end_time is not None and float(t) > float(record_end_time):
                running = False
                break
            if record_end_time is None and float(t) > float(chart_end):
                running = False
                break

        if (not advance_active) and end_time_sec is not None and float(t) > float(end_time_sec):
            try:
                audio.stop_music()
            except:
                pass
            running = False
            break

        overrender = float(getattr(args, "overrender", 2.0) or 2.0)
        if getattr(state, "render_overrender", None) is not None:
            try:
                overrender = float(getattr(state, "render_overrender"))
            except:
                pass
        if overrender < 1.0:
            overrender = 1.0
        RW = int(W * overrender)
        RH = int(H * overrender)

        mb_samples = 1
        mb_shutter = 0.0
        if getattr(state, "motion_blur_samples", None) is not None:
            try:
                mb_samples = max(1, int(getattr(state, "motion_blur_samples")))
            except:
                mb_samples = 1
        if getattr(state, "motion_blur_shutter", None) is not None:
            try:
                mb_shutter = clamp(float(getattr(state, "motion_blur_shutter")), 0.0, 2.0)
            except:
                mb_shutter = 0.0

        def _render_frame(t_draw: float) -> Tuple[pygame.Surface, List[Tuple[int, pygame.Surface, float, float]]]:
            nonlocal last_debug_ms
            nonlocal bg_scaled_cache_key
            nonlocal bg_scaled_cache
            nonlocal dim_surf_cache_key
            nonlocal dim_surf_cache

            base = surface_pool.get(RW, RH, pygame.SRCALPHA)
            if bg_blurred:
                key = (id(bg_blurred), int(RW), int(RH))
                if bg_scaled_cache is None or bg_scaled_cache_key != key:
                    bg_scaled_cache = pygame.transform.smoothscale(bg_blurred, (RW, RH))
                    bg_scaled_cache_key = key
                base.blit(bg_scaled_cache, (0, 0))
            else:
                base.fill((10, 10, 14))

            dim = bg_dim_alpha if (bg_dim_alpha is not None) else clamp(getattr(args, "bg_dim", 120), 0, 255)
            if dim > 0:
                dkey = (int(RW), int(RH), int(dim))
                if dim_surf_cache is None or dim_surf_cache_key != dkey:
                    dim_surf_cache = pygame.Surface((RW, RH), pygame.SRCALPHA)
                    dim_surf_cache.fill((0, 0, 0, int(dim)))
                    dim_surf_cache_key = dkey
                base.blit(dim_surf_cache, (0, 0))

            overlay = surface_pool.get(RW, RH, pygame.SRCALPHA)

            line_text_draw_calls: List[Tuple[int, pygame.Surface, float, float]] = []

            line_states: List[Tuple[float, float, float, float, float, float]] = []
            line_trig: List[Tuple[float, float]] = []
            for ln in lines:
                lx, ly, lr, la01, sc, la_raw = eval_line_state(ln, t_draw)
                line_states.append((lx, ly, lr, la01, sc, la_raw))
                line_trig.append((math.cos(lr), math.sin(lr)))

            try:
                flow_mul = float(getattr(state, "note_flow_speed_multiplier", 1.0) or 1.0)
            except:
                flow_mul = 1.0
            hold_keep_head = bool(state.respack and getattr(state.respack, "hold_keep_head", False))
            speed_mul_affects_travel = bool(getattr(state, "note_speed_mul_affects_travel", False))

            # Draw judge lines
            for ln, (lx, ly, lr, la01, _sc, _la_raw) in zip(lines, line_states):
                if la01 <= 1e-6:
                    continue

                if getattr(ln, "text", None) is not None:
                    try:
                        s = str(ln.text.eval(t_draw) if hasattr(ln.text, "eval") else "")
                    except:
                        s = ""
                    rr, gg, bb = (255, 255, 255)
                    surf_lines = s.split("\n")
                    y_off = 0
                    for part in surf_lines:
                        if not part:
                            y_off += int(small.get_linesize())
                            continue
                        txt = small.render(part, True, (int(rr), int(gg), int(bb)))
                        try:
                            txt.set_alpha(int(255 * la01))
                        except:
                            pass
                        overlay.blit(txt, (int(lx * overrender), int((ly + y_off) * overrender)))
                        y_off += int(small.get_linesize())

                if getattr(ln, "texture_path", None):
                    fp = str(getattr(ln, "texture_path"))
                    if not os.path.isabs(fp):
                        fp = os.path.join(chart_dir, fp)
                    img = None
                    if os.path.exists(fp):
                        img = line_tex_cache.get(fp)
                        if img is None:
                            try:
                                img = pygame.image.load(fp).convert_alpha()
                                line_tex_cache[fp] = img
                            except:
                                img = None
                    if img is not None:
                        try:
                            ax, ay = getattr(ln, "anchor", (0.5, 0.5))
                            ax = float(ax)
                            ay = float(ay)
                        except:
                            ax, ay = 0.5, 0.5
                        sx_tex = 1.0
                        sy_tex = 1.0
                        try:
                            if getattr(ln, "scale_x", None) is not None:
                                sx_tex = float(ln.scale_x.eval(t_draw) if hasattr(ln.scale_x, "eval") else 1.0)
                        except:
                            sx_tex = 1.0
                        try:
                            if getattr(ln, "scale_y", None) is not None:
                                sy_tex = float(ln.scale_y.eval(t_draw) if hasattr(ln.scale_y, "eval") else 1.0)
                        except:
                            sy_tex = 1.0
                        iw, ih = img.get_width(), img.get_height()
                        target_w = max(1, int((line_len * sx_tex) * overrender / float(expand)))
                        target_h = max(1, int((target_w * ih / max(1, iw)) * sy_tex))

                        # Cache smoothscale operation
                        img_id = id(img)
                        scaled = transform_cache.get_scaled(img, target_w, target_h, img_id)
                        if scaled is None:
                            scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                            transform_cache.put_scaled(img, target_w, target_h, img_id, scaled)

                        # Cache rotation operation
                        angle_deg = -lr * 180.0 / math.pi
                        scaled_id = id(scaled)
                        rotated = transform_cache.get_rotated(scaled, angle_deg, scaled_id)
                        if rotated is None:
                            rotated = pygame.transform.rotate(scaled, angle_deg)
                            transform_cache.put_rotated(scaled, angle_deg, scaled_id, rotated)

                        rotated.set_alpha(int(255 * la01))
                        axc = (ax - 0.5) * float(target_w)
                        ayc = (ay - 0.5) * float(target_h)
                        c0 = math.cos(-lr)
                        s0 = math.sin(-lr)
                        dx = c0 * axc - s0 * ayc
                        dy = s0 * axc + c0 * ayc
                        cx, cy = apply_expand_xy(lx * overrender, ly * overrender, RW, RH, expand)
                        overlay.blit(rotated, (cx - rotated.get_width() / 2 - dx, cy - rotated.get_height() / 2 - dy))
                        continue

                sx = 1.0
                sy = 1.0
                try:
                    if getattr(ln, "scale_x", None) is not None:
                        sx = float(ln.scale_x.eval(t_draw) if hasattr(ln.scale_x, "eval") else 1.0)
                except:
                    sx = 1.0
                try:
                    if getattr(ln, "scale_y", None) is not None:
                        sy = float(ln.scale_y.eval(t_draw) if hasattr(ln.scale_y, "eval") else 1.0)
                except:
                    sy = 1.0

                if sx <= 1e-6:
                    sx = 1.0
                if sy <= 1e-6:
                    sy = 1.0

                tx, ty = math.cos(lr), math.sin(lr)
                ex = tx * (line_len * sx) * 0.5
                ey = ty * (line_len * 0.5)
                p0 = (lx - ex, ly - ey)
                p1 = (lx + ex, ly + ey)
                p0s = apply_expand_xy(p0[0] * overrender, p0[1] * overrender, RW, RH, expand)
                p1s = apply_expand_xy(p1[0] * overrender, p1[1] * overrender, RW, RH, expand)
                rgba = (*ln.color_rgb, int(255 * la01))
                draw_line_rgba(overlay, p0s, p1s, rgba, width=line_w)
                lxs, lys = apply_expand_xy(lx * overrender, ly * overrender, RW, RH, expand)
                pygame.draw.circle(overlay, (*ln.color_rgb, int(220 * la01)), (int(lxs), int(lys)), dot_r)

                pr = int(line_last_hit_ms.get(ln.lid, 0))
                if getattr(args, "debug_line_label", False):
                    label = ln.name.strip() if ln.name.strip() else str(ln.lid)
                    txt = small.render(label, True, (240, 240, 240))
                    lxs, lys = apply_expand_xy(lx * overrender, ly * overrender, RW, RH, expand)
                    line_text_draw_calls.append((pr, txt, (lxs - txt.get_width() / 2) / overrender, (lys - txt.get_height() / 2) / overrender))

            # draw notes
            nonlocal note_render_count_last
            note_render_count = 0
            note_dbg_drawn = 0
            no_cull_all = bool(getattr(args, "no_cull", False))
            no_cull_screen = bool(getattr(args, "no_cull_screen", False))
            no_cull_enter_time = bool(getattr(args, "no_cull_enter_time", False))
            _st0 = max(0, idx_next - 400)
            _st1 = min(len(states), idx_next + 1200)
            for _si in range(int(_st0), int(_st1)):
                s = states[_si]
                n = s.note
                if n.kind != 3 and s.judged:
                    if bool(getattr(s, "miss", False)):
                        mt = getattr(s, "miss_t", None)
                        if mt is None:
                            continue
                        if float(t_draw) <= float(mt) + float(MISS_FADE_SEC):
                            pass
                        else:
                            continue
                    else:
                        continue
                if n.kind == 3 and bool(getattr(s, "hold_finalized", False)):
                    continue
                if n.fake:
                    continue
                if (not no_cull_all) and (not no_cull_enter_time):
                    if t_draw < float(n.t_enter):
                        continue
                    # Holds must remain visible until their end time (t_hit + hold duration).
                    t_end_for_cull = float(n.t_end) if int(n.kind) == 3 else float(n.t_hit)
                    extra_after = max(0.25, float(getattr(args, "approach", 3.0)) + 0.5)
                    if int(n.kind) == 3:
                        extra_after = 0.35
                    if t_draw > t_end_for_cull + float(extra_after):
                        continue

                note_render_count += 1

                ln = lines[n.line_id]
                lx, ly, lr, la01, sc_now, la_raw = line_states[n.line_id]
                tx, ty = line_trig[n.line_id]
                nx, ny = -ty, tx

                if getattr(args, "basic_debug", False):
                    now_ms = int(float(t_draw) * 1000.0)
                    if (now_ms - int(last_debug_ms)) >= 500:
                        try:
                            dy_dbg = float(n.scroll_hit) - float(sc_now)
                            print(
                                f"[dbg] t={float(t_draw):.3f} note={int(n.nid)} line={int(n.line_id)} t_hit={float(n.t_hit):.3f} "
                                f"sc_now={float(sc_now):.3f} sc_hit={float(n.scroll_hit):.3f} dy={float(dy_dbg):.3f}"
                            )
                        except:
                            pass
                        last_debug_ms = int(now_ms)

                note_alpha = clamp(float(getattr(n, "alpha01", 1.0)), 0.0, 1.0)
                if la01 < 0.0:
                    if str(getattr(args, "line_alpha_affects_notes", "negative_only")) != "never":
                        note_alpha *= clamp(1.0 + la01, 0.0, 1.0)
                elif str(getattr(args, "line_alpha_affects_notes", "negative_only")) == "always":
                    note_alpha *= clamp(la01, 0.0, 1.0)
                if note_alpha <= 1e-6:
                    continue

                miss_dim = 0.0
                if bool(getattr(s, "miss", False)):
                    mt = getattr(s, "miss_t", None)
                    if mt is not None:
                        dtm = float(t_draw) - float(mt)
                        if dtm >= 0.0:
                            miss_dim = clamp(dtm / float(MISS_FADE_SEC), 0.0, 1.0)
                            note_alpha *= (1.0 - miss_dim) * 0.65

                ws = float(base_note_w) * float(note_scale_x) * float(getattr(n, "size_px", 1.0))
                hs = float(base_note_h) * float(note_scale_y) * float(getattr(n, "size_px", 1.0))
                rgba_fill = (255, 255, 255, int(255 * note_alpha))
                rgba_outline = (0, 0, 0, int(220 * note_alpha))

                if n.kind == 3:
                    hit_for_draw = bool(s.hit) and (not bool(getattr(n, "fake", False)))
                    if hit_for_draw and respack and bool(getattr(respack, "hold_keep_head", False)):
                        dy = (float(sc_now) - float(sc_now)) * float(flow_mul)
                        if hold_keep_head and dy < 0.0:
                            dy = 0.0
                        y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy + float(getattr(n, "y_offset_px", 0.0))
                        x_local = float(getattr(n, "x_local_px", 0.0))
                        head = (float(lx) + float(tx) * x_local + float(nx) * y_local, float(ly) + float(ty) * x_local + float(ny) * y_local)
                    else:
                        head_target_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
                        dy = (float(head_target_scroll) - float(sc_now)) * float(flow_mul)
                        if hold_keep_head and dy < 0.0:
                            dy = 0.0
                        y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy + float(getattr(n, "y_offset_px", 0.0))
                        x_local = float(getattr(n, "x_local_px", 0.0))
                        head = (float(lx) + float(tx) * x_local + float(nx) * y_local, float(ly) + float(ty) * x_local + float(ny) * y_local)

                    dy = (float(getattr(n, "scroll_end", 0.0)) - float(sc_now)) * float(flow_mul)
                    mult = max(0.0, float(getattr(n, "speed_mul", 1.0)))
                    y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy * mult + float(getattr(n, "y_offset_px", 0.0))
                    x_local = float(getattr(n, "x_local_px", 0.0))
                    tail = (float(lx) + float(tx) * x_local + float(nx) * y_local, float(ly) + float(ty) * x_local + float(ny) * y_local)
                    head_s = apply_expand_xy(head[0] * overrender, head[1] * overrender, RW, RH, expand)
                    tail_s = apply_expand_xy(tail[0] * overrender, tail[1] * overrender, RW, RH, expand)

                    if (not no_cull_all) and (not no_cull_screen):
                        m = int(120 * overrender)
                        minx = min(float(head_s[0]), float(tail_s[0]))
                        maxx = max(float(head_s[0]), float(tail_s[0]))
                        miny = min(float(head_s[1]), float(tail_s[1]))
                        maxy = max(float(head_s[1]), float(tail_s[1]))
                        if maxx < -m or minx > float(RW + m) or maxy < -m or miny > float(RH + m):
                            continue

                    hold_alpha = note_alpha
                    if s.hold_failed:
                        hold_alpha *= 0.35
                    mh = bool(getattr(n, "mh", False))
                    size_scale = float(getattr(n, "size_px", 1.0) or 1.0)
                    note_rgb = getattr(n, "tint_rgb", (255, 255, 255))
                    line_rgb = ln.color_rgb
                    prog = None
                    try:
                        if (
                            bool(getattr(s, "hit", False))
                            or bool(getattr(s, "holding", False))
                            or (float(t_draw) >= float(n.t_hit))
                        ):
                            den = float(n.scroll_end) - float(n.scroll_hit)
                            num = float(sc_now) - float(n.scroll_hit)
                            if abs(den) > 1e-6:
                                prog = clamp(num / den, 0.0, 1.0)
                            else:
                                dur_t = float(n.t_end) - float(n.t_hit)
                                if dur_t > 1e-6:
                                    prog = clamp((t_draw - n.t_hit) / dur_t, 0.0, 1.0)
                    except:
                        prog = None
                    draw_hold_3slice(
                        overlay=overlay,
                        head_xy=head_s,
                        tail_xy=tail_s,
                        line_rot=lr,
                        alpha01=hold_alpha,
                        line_rgb=(int(line_rgb[0]), int(line_rgb[1]), int(line_rgb[2])),
                        note_rgb=(int(note_rgb[0]), int(note_rgb[1]), int(note_rgb[2])),
                        size_scale=size_scale,
                        mh=mh,
                        hold_body_w=max(1, int(hold_body_w * overrender)),
                        progress=prog,
                        draw_outline=(not getattr(args, "no_note_outline", False)),
                        outline_width=max(1, int(outline_w * overrender)),
                    )

                    if getattr(args, "debug_note_info", False):
                        if int(note_dbg_drawn) >= 80:
                            pass
                        else:
                            try:
                                dy_dbg = float(n.scroll_hit) - float(sc_now)
                                dt_ms = (float(t_draw) - float(n.t_hit)) * 1000.0
                                side_ch = "A" if bool(getattr(n, "above", True)) else "B"
                                label_key = f"{int(n.nid)}:{int(n.kind)} L{int(n.line_id)}{side_ch}"
                                surf = note_dbg_cache.get(label_key)
                                if surf is None:
                                    surf = small.render(label_key, True, (240, 240, 240))
                                    note_dbg_cache[label_key] = surf
                                extra = f"dt={dt_ms:+.0f}ms dy={float(dy_dbg):.1f}"
                                if prog is not None:
                                    extra += f" p={float(prog)*100.0:4.1f}%"
                                surf2 = small.render(extra, True, (200, 200, 200))
                                nx = -math.sin(float(lr))
                                ny = math.cos(float(lr))
                                side = 1.0 if bool(getattr(n, "above", True)) else -1.0
                                off = (float(hs) * float(overrender) * 0.8 + 14.0 * float(overrender))
                                tx = float(head_s[0]) + nx * off * side
                                ty = float(head_s[1]) + ny * off * side
                                overlay.blit(surf, (int(tx - surf.get_width() / 2), int(ty - surf.get_height() / 2)))
                                overlay.blit(surf2, (int(tx - surf2.get_width() / 2), int(ty - surf2.get_height() / 2 + surf.get_height())))
                                note_dbg_drawn += 1
                            except:
                                pass
                else:
                    dy = (float(getattr(n, "scroll_hit", 0.0)) - float(sc_now)) * float(flow_mul)
                    mult = 1.0
                    if speed_mul_affects_travel:
                        mult = max(0.0, float(getattr(n, "speed_mul", 1.0)))
                    y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy * float(mult) + float(getattr(n, "y_offset_px", 0.0))
                    x_local = float(getattr(n, "x_local_px", 0.0))
                    p = (float(lx) + float(tx) * x_local + float(nx) * y_local, float(ly) + float(ty) * x_local + float(ny) * y_local)
                    ps = apply_expand_xy(p[0] * overrender, p[1] * overrender, RW, RH, expand)

                    if (not no_cull_all) and (not no_cull_screen):
                        m = int(120 * overrender)
                        if (float(ps[0]) < -m) or (float(ps[0]) > float(RW + m)) or (float(ps[1]) < -m) or (float(ps[1]) > float(RH + m)):
                            continue

                    img = pick_note_image(n, respack)
                    if img is None:
                        if miss_dim > 1e-6:
                            g = int(255 * (1.0 - 0.6 * float(miss_dim)))
                            rgba_fill = (g, g, g, int(255 * note_alpha))
                            rgba_outline = (0, 0, 0, int(220 * note_alpha))
                        pts = rect_corners(ps[0], ps[1], ws * overrender, hs * overrender, lr)
                        draw_poly_rgba(overlay, pts, rgba_fill)
                        if not getattr(args, "no_note_outline", False):
                            draw_poly_outline_rgba(overlay, pts, rgba_outline, width=outline_w)
                    else:
                        iw, ih = img.get_width(), img.get_height()
                        target_w = max(1, int(ws * overrender))
                        target_h = max(1, int(target_w * ih / max(1, iw) * note_scale_y))

                        # Cache note texture transforms
                        img_id = id(img)
                        scaled = transform_cache.get_scaled(img, target_w, target_h, img_id)
                        if scaled is None:
                            scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                            transform_cache.put_scaled(img, target_w, target_h, img_id, scaled)

                        angle_deg = -lr * 180.0 / math.pi
                        # IMPORTANT: use a stable id for the scaled variant to avoid cache key collisions
                        # (id(scaled) is not stable across frames and may be reused by Python)
                        scaled_key_id = hash((int(img_id), int(target_w), int(target_h)))
                        rotated = transform_cache.get_rotated(scaled, angle_deg, scaled_key_id)
                        if rotated is None:
                            rotated = pygame.transform.rotate(scaled, angle_deg)
                            transform_cache.put_rotated(scaled, angle_deg, scaled_key_id, rotated)

                        try:
                            tr, tg, tb = getattr(n, "tint_rgb", (255, 255, 255))
                            if miss_dim > 1e-6:
                                g = int(220 * (1.0 - 0.7 * float(miss_dim)))
                                tr = int(tr * (1.0 - 0.8 * float(miss_dim)) + g * (0.8 * float(miss_dim)))
                                tg = int(tg * (1.0 - 0.8 * float(miss_dim)) + g * (0.8 * float(miss_dim)))
                                tb = int(tb * (1.0 - 0.8 * float(miss_dim)) + g * (0.8 * float(miss_dim)))
                            rotated.fill((int(tr), int(tg), int(tb), 255), special_flags=pygame.BLEND_RGBA_MULT)
                        except:
                            pass
                        rotated.set_alpha(int(255 * note_alpha))
                        overlay.blit(rotated, (ps[0] - rotated.get_width() / 2, ps[1] - rotated.get_height() / 2))
                        pts = rect_corners(ps[0], ps[1], float(target_w), float(target_h), lr)
                        if not getattr(args, "no_note_outline", False):
                            draw_poly_outline_rgba(overlay, pts, rgba_outline, width=outline_w)

                    if getattr(args, "debug_note_info", False):
                        if int(note_dbg_drawn) >= 80:
                            pass
                        else:
                            try:
                                dy_dbg = float(n.scroll_hit) - float(sc_now)
                                dt_ms = (float(t_draw) - float(n.t_hit)) * 1000.0
                                side_ch = "A" if bool(getattr(n, "above", True)) else "B"
                                label_key = f"{int(n.nid)}:{int(n.kind)} L{int(n.line_id)}{side_ch}"
                                surf = note_dbg_cache.get(label_key)
                                if surf is None:
                                    surf = small.render(label_key, True, (240, 240, 240))
                                    note_dbg_cache[label_key] = surf
                                extra = f"dt={dt_ms:+.0f}ms dy={float(dy_dbg):.1f}"
                                surf2 = small.render(extra, True, (200, 200, 200))
                                nx = -math.sin(float(lr))
                                ny = math.cos(float(lr))
                                side = 1.0 if bool(getattr(n, "above", True)) else -1.0
                                off = (float(hs) * float(overrender) * 0.8 + 14.0 * float(overrender))
                                tx = float(ps[0]) + nx * off * side
                                ty = float(ps[1]) + ny * off * side
                                overlay.blit(surf, (int(tx - surf.get_width() / 2), int(ty - surf.get_height() / 2)))
                                overlay.blit(surf2, (int(tx - surf2.get_width() / 2), int(ty - surf2.get_height() / 2 + surf.get_height())))
                                note_dbg_drawn += 1
                            except:
                                pass

            # hitfx
            hitfx[:] = prune_hitfx(hitfx, t_draw, (respack.hitfx_duration if respack else 0.18))
            for fx in hitfx:
                draw_hitfx(
                    overlay,
                    fx,
                    t_draw,
                    respack=respack,
                    W=RW,
                    H=RH,
                    expand=expand,
                    hitfx_scale_mul=float(getattr(args, "hitfx_scale_mul", 1.0)),
                    overrender=float(overrender),
                )

            # BAD ghost indicators
            if bad_ghosts:
                kept: List[Dict[str, Any]] = []
                for g in bad_ghosts:
                    try:
                        dtg = float(t_draw) - float(g.get("t0", 0.0))
                        if dtg < 0.0 or dtg > float(BAD_GHOST_SEC):
                            continue
                        a01 = clamp(1.0 - dtg / float(BAD_GHOST_SEC), 0.0, 1.0)
                        nx = float(g.get("x", 0.0))
                        ny = float(g.get("y", 0.0))
                        nr = float(g.get("rot", 0.0))
                        nn = g.get("note", None)
                        if nn is None:
                            continue
                        ps = apply_expand_xy(nx * overrender, ny * overrender, RW, RH, expand)
                        img = pick_note_image(nn, respack)
                        ws = float(base_note_w) * float(note_scale_x) * float(getattr(nn, "size_px", 1.0))
                        hs = float(base_note_h) * float(note_scale_y) * float(getattr(nn, "size_px", 1.0))
                        if img is None:
                            pts = rect_corners(ps[0], ps[1], ws * overrender, hs * overrender, nr)
                            draw_poly_rgba(overlay, pts, (255, 80, 80, int(180 * a01)))
                            if not getattr(args, "no_note_outline", False):
                                draw_poly_outline_rgba(overlay, pts, (0, 0, 0, int(160 * a01)), width=outline_w)
                        else:
                            iw, ih = img.get_width(), img.get_height()
                            target_w = max(1, int(ws * overrender))
                            target_h = max(1, int(target_w * ih / max(1, iw) * note_scale_y))
                            img_id = id(img)
                            scaled = transform_cache.get_scaled(img, target_w, target_h, img_id)
                            if scaled is None:
                                scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                                transform_cache.put_scaled(img, target_w, target_h, img_id, scaled)
                            angle_deg = -nr * 180.0 / math.pi
                            scaled_key_id = hash((int(img_id), int(target_w), int(target_h)))
                            rotated = transform_cache.get_rotated(scaled, angle_deg, scaled_key_id)
                            if rotated is None:
                                rotated = pygame.transform.rotate(scaled, angle_deg)
                                transform_cache.put_rotated(scaled, angle_deg, scaled_key_id, rotated)
                            try:
                                rg = rotated.copy()
                                rg.fill((255, 80, 80, 255), special_flags=pygame.BLEND_RGBA_MULT)
                                rg.set_alpha(int(200 * a01))
                                overlay.blit(rg, (ps[0] - rg.get_width() / 2, ps[1] - rg.get_height() / 2))
                            except:
                                pass
                        kept.append(g)
                    except:
                        continue
                bad_ghosts[:] = kept

            base.blit(overlay, (0, 0))
            surface_pool.release(overlay)
            note_render_count_last = int(note_render_count)

            return base, line_text_draw_calls

        if "line_last_hit_ms" not in locals():
            line_last_hit_ms: Dict[int, int] = {}

        def _mark_line_hit(lid: int, now_ms: int):
            line_last_hit_ms[lid] = int(now_ms)

        def _push_hit_debug(*, t_now: float, t_hit: float, note_id: int, judgement: str, hold_percent: Optional[float] = None):
            nonlocal hit_debug_seq
            if not hit_debug:
                return
            hit_debug_seq += 1
            dt_ms = (float(t_now) - float(t_hit)) * 1000.0
            hp = None
            if hold_percent is not None:
                try:
                    hp = clamp(float(hold_percent), 0.0, 1.0)
                except:
                    hp = None
            hit_debug_lines.appendleft({
                "seq": int(hit_debug_seq),
                "dt_ms": float(dt_ms),
                "nid": int(note_id),
                "judgement": str(judgement),
                "hold_percent": hp,
            })

        # Autoplay
        if getattr(args, "autoplay", False):
            if "prev_autoplay_t" not in locals():
                prev_autoplay_t = float(t) - 1e-6
            _st0 = max(0, idx_next - 20)
            _st1 = min(len(states), idx_next + 300)
            for _si in range(int(_st0), int(_st1)):
                s = states[_si]
                if s.judged or s.note.fake:
                    continue
                n = s.note
                if n.kind != 3:
                    act = None
                    if judge_plan is not None:
                        try:
                            act = judge_plan.action_for(n)
                        except:
                            act = None
                    dt_ms = float(getattr(act, "dt_ms", 0.0) if act is not None else 0.0)
                    grade0 = getattr(act, "grade", None) if act is not None else "PERFECT"
                    if (grade0 is not None) and str(grade0).upper() == "MISS":
                        grade = "MISS"
                    else:
                        grade = _sanitize_grade(int(n.kind), grade0)
                    t_hit = float(n.t_hit) + dt_ms / 1000.0

                    if (grade is not None) and float(prev_autoplay_t) < float(t_hit) <= float(t):
                        if str(grade).upper() == "MISS":
                            try:
                                setattr(s, "miss_t", float(t_hit))
                            except:
                                pass
                            s.miss = True
                            s.judged = True
                            judge.mark_miss(s)
                            _push_hit_debug(t_now=float(t_hit), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement="MISS")
                            continue
                        _apply_grade(s, str(grade))
                        s.judged = True
                        s.hit = True
                        ln = lines[n.line_id]
                        t_fx = float(t_hit)
                        lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t_fx)
                        x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        elif respack:
                            c = respack.judge_colors.get("PERFECT", c)
                        hitfx.append(HitFX(x, y, t_fx, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, int(t_fx * 1000.0), int(respack.hitfx_duration * 1000), c))
                        _mark_line_hit(n.line_id, int(t_fx * 1000.0))
                        _push_hit_debug(t_now=float(t_fx), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement=str(grade))
                        if not record_enabled:
                            hitsound.play(n, int(t_fx * 1000.0), respack=respack)
                else:
                    act = None
                    if judge_plan is not None:
                        try:
                            act = judge_plan.action_for(n)
                        except:
                            act = None
                    dt_ms = float(getattr(act, "dt_ms", 0.0) if act is not None else 0.0)
                    grade0 = getattr(act, "grade", None) if act is not None else "PERFECT"
                    if (grade0 is not None) and str(grade0).upper() == "MISS":
                        grade = "MISS"
                    else:
                        grade = _sanitize_grade(int(n.kind), grade0)
                    hp = getattr(act, "hold_percent", None) if act is not None else None
                    t_hit = float(n.t_hit) + dt_ms / 1000.0
                    if hp is None:
                        hp = 1.0

                    if (not s.holding) and str(grade).upper() == "MISS" and float(prev_autoplay_t) < float(t_hit) <= float(t):
                        try:
                            setattr(s, "miss_t", float(t_hit))
                        except:
                            pass
                        s.miss = True
                        s.judged = True
                        s.hold_failed = True
                        s.hold_finalized = True
                        s.holding = False
                        judge.mark_miss(s)
                        _push_hit_debug(t_now=float(t_hit), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement="MISS", hold_percent=None)
                        continue

                    if (not s.holding) and (grade is not None) and float(prev_autoplay_t) < float(t_hit) <= float(t):
                        s.hit = True
                        s.holding = True
                        s.hold_grade = str(grade)
                        # Hold counts into combo at press time
                        judge.bump()
                        t_fx = float(t_hit)
                        s.next_hold_fx_ms = int(t_fx * 1000.0) + hold_fx_interval_ms
                        ln = lines[n.line_id]
                        lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t_fx)
                        x, y = note_world_pos(lx, ly, lr, sc, n, sc, for_tail=False)
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        elif respack:
                            c = respack.judge_colors.get(grade, c)
                            c = respack.judge_colors.get("PERFECT", c)
                        hitfx.append(HitFX(x, y, t_fx, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, int(t_fx * 1000.0), int(respack.hitfx_duration * 1000), c))
                        _push_hit_debug(t_now=float(t_fx), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement=str(grade), hold_percent=0.0)
                        if not record_enabled:
                            hitsound.play(n, int(t_fx * 1000.0), respack=respack)

                    if s.holding:
                        dur = max(1e-6, float(n.t_end) - float(n.t_hit))
                        t_rel = float(n.t_hit) + float(hp) * dur
                        if float(t) >= float(t_rel) and float(t_rel) >= float(n.t_hit) and float(t) < float(n.t_end) - 1e-6:
                            try:
                                s.released_early = True
                                setattr(s, "release_t", float(t_rel))
                                setattr(s, "release_percent", float(hp))
                            except:
                                pass
                            if float(hp) < float(hold_tail_tol):
                                try:
                                    setattr(s, "miss_t", float(t_rel))
                                except:
                                    pass
                                s.miss = True
                                s.judged = True
                                s.hold_failed = True
                                s.hold_finalized = True
                                s.holding = False
                                judge.mark_miss(s)
                            else:
                                s.holding = False
                    if s.holding and t >= n.t_end:
                        s.holding = False

            prev_autoplay_t = float(t)

        # Manual judgement (pointer-driven)
        # - tap: press/release without flick
        # - flick: move >= flick_threshold*W during a press, then release
        # - hold: long press on hold note head (kind=3)
        # - drag: holding (down) can judge kind=2 notes
        if not getattr(args, "autoplay", False):
            # keyboard SPACE fallback: has no position, only timing-based tap/hold/drag/flick selection is ambiguous.
            down = input_down()
            press_edge = down and not prev_down
            release_edge = (not down) and prev_down
            prev_down = down

            # If using mouse pointer, trigger on release to disambiguate flick vs tap.
            if pointer_up_fire:
                try:
                    flick_thr_px = float(getattr(args, "flick_threshold", 0.02)) * float(W)
                except Exception:
                    flick_thr_px = 0.02 * float(W)

                is_flick = bool(pointer_moved_px >= float(flick_thr_px))
                pointer_tap_fire = not is_flick
                pointer_flick_fire = is_flick
                pointer_up_fire = False

            # Gesture kind for this frame (None means no new gesture)
            gesture: Optional[str] = None
            if pointer_flick_fire:
                gesture = "flick"
                pointer_flick_fire = False
            elif pointer_tap_fire:
                gesture = "tap"
                pointer_tap_fire = False
            elif press_edge:
                # keyboard press: treat as tap trigger
                gesture = "tap"

            # drag/hold are continuous while holding; evaluate every frame
            hold_like_down = bool(down)

            # judgement width config (horizontal, in pixels) for pointer-based hits
            try:
                judge_w_px = float(getattr(args, "judge_width", 0.12)) * float(W)
            except Exception:
                judge_w_px = 0.12 * float(W)
            if judge_w_px < 1.0:
                judge_w_px = 1.0

            def _note_x_at_time(n: RuntimeNote, tt: float) -> float:
                ln = lines[int(n.line_id)]
                lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, tt)
                scroll_target = float(n.scroll_hit)
                xw, yw = note_world_pos(lx, ly, lr, sc_now, n, scroll_target, for_tail=False)
                return float(xw)

            def _in_judge_width(n: RuntimeNote, tt: float, pointer_x: Optional[float]) -> bool:
                if pointer_x is None:
                    return True
                try:
                    nx = _note_x_at_time(n, tt)
                except Exception:
                    return True
                return abs(float(pointer_x) - float(nx)) <= float(judge_w_px) * 0.5

            def _pick_best_candidate(allow_kinds: set[int], tt: float, pointer_x: Optional[float]) -> Optional[NoteState]:
                best_s: Optional[NoteState] = None
                best_dt = 1e9
                _st0 = max(0, idx_next - 80)
                _st1 = min(len(states), idx_next + 900)
                for _si in range(int(_st0), int(_st1)):
                    s = states[_si]
                    if s.judged or s.note.fake:
                        continue
                    n = s.note
                    if int(n.kind) not in allow_kinds:
                        continue
                    dt = abs(float(tt) - float(n.t_hit))
                    if dt > float(Judge.BAD):
                        continue
                    if not _in_judge_width(n, tt, pointer_x):
                        continue
                    if dt < best_dt:
                        best_dt = dt
                        best_s = s
                return best_s

            # Pointer X in world coordinates: screen coordinate is already in window space.
            pointer_x: Optional[float] = None
            if pointer_last_pos is not None:
                try:
                    pointer_x = float(pointer_last_pos[0])
                except Exception:
                    pointer_x = None

            # 1) discrete gesture judgement (tap/flick)
            if gesture is not None:
                if gesture == "tap":
                    cand = _pick_best_candidate({1}, float(t), pointer_x)
                elif gesture == "flick":
                    cand = _pick_best_candidate({4}, float(t), pointer_x)
                else:
                    cand = None

                if cand is not None:
                    n = cand.note
                    grade = judge.grade_window(float(n.t_hit), float(t))
                    if grade is not None:
                        apply_grade(cand, str(grade), judge)
                        ln = lines[n.line_id]
                        lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        elif respack:
                            c = (
                                respack.judge_colors.get(grade)
                                or respack.judge_colors.get("GOOD")
                                or respack.judge_colors.get("PERFECT")
                                or c
                            )
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                        _mark_line_hit(n.line_id, int(t * 1000.0))
                        _push_hit_debug(t_now=float(t), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement=str(grade))
                        if not record_enabled:
                            hitsound.play(n, int(t * 1000.0), respack=respack)

            # 2) continuous drag judgement: holding down can judge kind=2
            if hold_like_down:
                cand_drag = _pick_best_candidate({2}, float(t), pointer_x)
                if cand_drag is not None:
                    n = cand_drag.note
                    # drag: only PERFECT/MISS (keep existing rule)
                    dt = abs(float(t) - float(n.t_hit))
                    if dt <= float(Judge.PERFECT):
                        apply_grade(cand_drag, "PERFECT", judge)
                        ln = lines[n.line_id]
                        lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        elif respack:
                            c = respack.judge_colors.get("PERFECT", c)
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                        _mark_line_hit(n.line_id, int(t * 1000.0))
                        _push_hit_debug(t_now=float(t), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement="PERFECT")
                        if not record_enabled:
                            hitsound.play(n, int(t * 1000.0), respack=respack)

            # 3) hold head judgement: press_edge triggers kind=3
            if press_edge:
                cand_hold = _pick_best_candidate({3}, float(t), pointer_x)
                if cand_hold is not None:
                    n = cand_hold.note
                    # Hold: PERFECT / GOOD (includes BAD window) / MISS
                    dt = abs(float(t) - float(n.t_hit))
                    grade_h = "PERFECT" if dt <= float(Judge.PERFECT) else ("GOOD" if dt <= float(Judge.BAD) else None)
                    if grade_h is not None:
                        cand_hold.hit = True
                        cand_hold.holding = True
                        cand_hold.hold_grade = str(grade_h)
                        judge.bump()
                        cand_hold.next_hold_fx_ms = int(t * 1000.0) + hold_fx_interval_ms
                        ln = lines[n.line_id]
                        lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        elif respack:
                            c = (
                                respack.judge_colors.get(grade_h)
                                or respack.judge_colors.get("GOOD")
                                or respack.judge_colors.get("PERFECT")
                                or c
                            )
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                        _mark_line_hit(n.line_id, int(t * 1000.0))
                        _push_hit_debug(t_now=float(t), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement=str(grade_h), hold_percent=0.0)
                        if not record_enabled:
                            hitsound.play(n, int(t * 1000.0), respack=respack)

        # hold maintenance
        if not getattr(args, "autoplay", False):
            _st0 = max(0, idx_next - 50)
            _st1 = min(len(states), idx_next + 500)
            for _si in range(int(_st0), int(_st1)):
                s = states[_si]
                if s.judged or s.note.fake:
                    continue
                n = s.note
                if n.kind == 3 and s.holding:
                    if (not input_down()) and t < n.t_end - 1e-6:
                        try:
                            dur = max(1e-6, float(n.t_end) - float(n.t_hit))
                            prog_r = clamp((float(t) - float(n.t_hit)) / dur, 0.0, 1.0)
                        except:
                            prog_r = 0.0
                        s.released_early = True
                        try:
                            setattr(s, "release_t", float(t))
                            setattr(s, "release_percent", float(prog_r))
                        except:
                            pass
                        if float(prog_r) < float(hold_tail_tol):
                            try:
                                setattr(s, "miss_t", float(t))
                            except:
                                pass
                            s.miss = True
                            s.judged = True
                            s.hold_failed = True
                            s.hold_finalized = True
                            s.holding = False
                            judge.mark_miss(s)
                        else:
                            s.holding = False
                    if t >= n.t_end:
                        s.holding = False

        # hold finalize
        _st0 = max(0, idx_next - 200)
        _st1 = min(len(states), idx_next + 800)
        for _si in range(int(_st0), int(_st1)):
            s = states[_si]
            n = s.note
            if n.fake or n.kind != 3 or s.hold_finalized:
                continue

            if (not s.hit) and (not s.hold_failed) and (t > n.t_hit + float(MISS_WINDOW)):
                s.hold_failed = True
                judge.break_combo()

            if s.released_early and (not s.hold_finalized):
                dur = max(1e-6, (n.t_end - n.t_hit))
                prog = clamp((t - n.t_hit) / dur, 0.0, 1.0)
                if prog < hold_tail_tol:
                    s.hold_failed = True
                    judge.break_combo()
                else:
                    g = s.hold_grade or "PERFECT"
                    judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    judge.judged_cnt += 1
                    s.hold_finalized = True
                    _push_hit_debug(t_now=float(t), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement=str(g), hold_percent=float(prog))

            if t >= n.t_end and (not s.hold_finalized):
                if s.hit and (not s.hold_failed):
                    g = s.hold_grade or "PERFECT"
                    judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    judge.judged_cnt += 1
                    dur = max(1e-6, (n.t_end - n.t_hit))
                    prog = clamp((t - n.t_hit) / dur, 0.0, 1.0)
                    _push_hit_debug(t_now=float(t), t_hit=float(n.t_hit), note_id=int(getattr(n, "nid", -1)), judgement=str(g), hold_percent=float(prog))
                else:
                    judge.mark_miss(s)
                s.hold_finalized = True
                s.judged = True

        # hold tick fx
        if respack:
            now_tick = int(t * 1000.0)
            _st0 = max(0, idx_next - 200)
            _st1 = min(len(states), idx_next + 800)
            for _si in range(int(_st0), int(_st1)):
                s = states[_si]
                n = s.note
                if n.fake or n.kind != 3 or (not s.holding) or s.judged:
                    continue
                if t >= n.t_end:
                    continue
                if s.next_hold_fx_ms <= 0:
                    s.next_hold_fx_ms = now_tick + hold_fx_interval_ms
                    continue
                while now_tick >= s.next_hold_fx_ms and t < n.t_end:
                    ln = lines[n.line_id]
                    lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                    x, y = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
                    c = respack.judge_colors.get("PERFECT", (255, 255, 255, 255))
                    if getattr(n, "tint_hitfx_rgb", None) is not None:
                        try:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        except:
                            pass
                    hitfx.append(HitFX(x, y, t, c, lr))
                    if not respack.hide_particles:
                        particles.append(ParticleBurst(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                    _mark_line_hit(n.line_id, int(t * 1000.0))
                    s.next_hold_fx_ms += hold_fx_interval_ms

        # miss detection
        miss_window = float(MISS_WINDOW)
        _st0 = max(0, idx_next - 200)
        _st1 = min(len(states), idx_next + 800)
        for _si in range(int(_st0), int(_st1)):
            s = states[_si]
            if s.judged or s.note.fake:
                continue
            if s.note.kind == 3:
                continue
            if t > s.note.t_hit + miss_window:
                try:
                    setattr(s, "miss_t", float(t))
                except:
                    pass
                judge.mark_miss(s)

        while idx_next < len(states) and states[idx_next].judged:
            idx_next += 1

        display_frame, line_text_draw_calls = _render_frame(t)

        # Debug: judge windows (draw judge area for each note)
        if getattr(args, "debug_judge_windows", False):
            try:
                try:
                    judge_w_px = float(getattr(args, "judge_width", 0.12)) * float(W)
                except Exception:
                    judge_w_px = 0.12 * float(W)
                if judge_w_px < 1.0:
                    judge_w_px = 1.0

                # We draw 3 time windows (PERFECT/GOOD/BAD) as rings around the note when the note is within that window.
                # This is a visual proxy for the judge area.
                _st0 = max(0, idx_next - 60)
                _st1 = min(len(states), idx_next + 700)
                for _si in range(int(_st0), int(_st1)):
                    s = states[_si]
                    if s.judged or s.note.fake:
                        continue
                    n = s.note
                    if int(getattr(n, "kind", 1)) == 3 and getattr(s, "holding", False):
                        continue

                    dt = abs(float(t) - float(n.t_hit))
                    if dt > float(Judge.BAD):
                        continue

                    ln = lines[n.line_id]
                    lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                    x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                    ps = apply_expand_xy(float(x) * float(overrender), float(y) * float(overrender), int(RW), int(RH), expand)

                    # represent judge width by a horizontal segment length
                    half_w = float(judge_w_px) * 0.5
                    p0 = apply_expand_xy((float(x) - half_w) * float(overrender), float(y) * float(overrender), int(RW), int(RH), expand)
                    p1 = apply_expand_xy((float(x) + half_w) * float(overrender), float(y) * float(overrender), int(RW), int(RH), expand)

                    a = int(120)
                    col_bad = (255, 80, 80, a)
                    col_good = (255, 210, 80, a)
                    col_perf = (80, 220, 255, a)
                    if dt <= float(Judge.BAD):
                        draw_line_rgba(display_frame, p0, p1, col_bad, width=max(1, int(2 * overrender)))
                    if dt <= float(Judge.GOOD):
                        draw_line_rgba(display_frame, p0, p1, col_good, width=max(1, int(3 * overrender)))
                    if dt <= float(Judge.PERFECT):
                        draw_line_rgba(display_frame, p0, p1, col_perf, width=max(1, int(4 * overrender)))

                    # mark note center
                    draw_ring(display_frame, float(ps[0]), float(ps[1]), int(6 * overrender), (255, 255, 255, 140), thickness=max(1, int(2 * overrender)))
            except Exception:
                pass

        if record_headless and record_enabled:
            if getattr(args, "record_render_particles", False):
                try:
                    now_ms = int(t * 1000.0)
                    particles[:] = prune_particles(particles, now_ms)
                    draw_particles(display_frame, particles, now_ms, W, H, expand)
                except:
                    pass

            if getattr(args, "record_render_text", False):
                try:
                    if line_text_draw_calls:
                        line_text_draw_calls.sort(key=lambda x: x[0])
                        for _pr, surf, x0, y0 in line_text_draw_calls:
                            display_frame.blit(surf, (x0, y0))
                except:
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

                    if chart_end > 1e-6:
                        pbar = progress_ratio(t, chart_end, advance_active=advance_active, start_time=getattr(args, "start_time", None))
                        pygame.draw.rect(display_frame, (40, 40, 40), pygame.Rect(0, 0, W, 6))
                        pygame.draw.rect(display_frame, (230, 230, 230), pygame.Rect(0, 0, int(W * pbar), 6))

                    combo_txt = font.render(f"COMBO {judge.combo}", True, (240, 240, 240))
                    display_frame.blit(combo_txt, (ui_x, ui_combo_y))

                    score, acc_ratio, _combo_ratio = compute_score(judge.acc_sum, judge.judged_cnt, judge.combo, judge.max_combo, total_notes)
                    score_txt = small.render(
                        f"SCORE {score:07d}   HIT {acc_ratio*100:6.2f}%   MAX {judge.max_combo}/{total_notes}",
                        True,
                        (200, 200, 200),
                    )
                    display_frame.blit(score_txt, (ui_x, ui_score_y))

                    fmt_txt = small.render(f"fmt={fmt}  t={t:7.3f}s  next={idx_next}/{len(states)}  lines={len(lines)}", True, (180, 180, 180))
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
                            except:
                                pass
                except:
                    pass
        display_frame_cur = pygame.transform.smoothscale(display_frame, (W, H))
        surface_pool.release(display_frame)
        if mb_samples > 1 and mb_shutter > 1e-6:
            acc = pygame.Surface((W, H), pygame.SRCALPHA)
            acc.fill((0, 0, 0, 0))
            dt_chart = float(_dt_frame) * chart_speed
            for i in range(mb_samples):
                frac = 0.0 if mb_samples <= 1 else (float(i) / float(mb_samples - 1))
                t_s = t - mb_shutter * dt_chart * (1.0 - frac)
                b_i, _ = _render_frame(t_s)
                f_i = pygame.transform.smoothscale(b_i, (W, H))
                surface_pool.release(b_i)
                f_i.set_alpha(int(255 / float(mb_samples)))
                acc.blit(f_i, (0, 0), special_flags=pygame.BLEND_RGBA_ADD)
            display_frame_cur = acc

        trail_alpha = clamp(float(getattr(args, "trail_alpha", 0.0) or 0.0), 0.0, 1.0)
        if getattr(state, "trail_alpha", None) is not None:
            try:
                trail_alpha = clamp(float(getattr(state, "trail_alpha")), 0.0, 1.0)
            except:
                pass

        trail_frames = 1
        if getattr(state, "trail_frames", None) is not None:
            try:
                trail_frames = max(1, int(getattr(state, "trail_frames")))
            except:
                trail_frames = 1

        trail_decay = 0.85
        if getattr(state, "trail_decay", None) is not None:
            try:
                trail_decay = clamp(float(getattr(state, "trail_decay")), 0.0, 1.0)
            except:
                trail_decay = 0.85

        trail_blur = int(getattr(args, "trail_blur", 0) or 0)
        if getattr(state, "trail_blur", None) is not None:
            try:
                trail_blur = int(getattr(state, "trail_blur"))
            except:
                pass
        trail_dim = clamp(int(getattr(args, "trail_dim", 0) or 0), 0, 255)
        if getattr(state, "trail_dim", None) is not None:
            try:
                trail_dim = clamp(int(getattr(state, "trail_dim")), 0, 255)
            except:
                pass

        trail_blur_ramp = False
        if getattr(state, "trail_blur_ramp", None) is not None:
            try:
                trail_blur_ramp = bool(getattr(state, "trail_blur_ramp"))
            except:
                trail_blur_ramp = False

        trail_blend = "normal"
        if getattr(state, "trail_blend", None) is not None:
            try:
                trail_blend = str(getattr(state, "trail_blend")).strip().lower()
            except:
                trail_blend = "normal"

        if trail_alpha > 1e-6 and trail_frames >= 1:
            if "trail_hist" not in locals():
                trail_hist = deque(maxlen=int(trail_frames))
                trail_hist_cap = int(trail_frames)
            if "trail_hist_cap" not in locals():
                trail_hist_cap = int(trail_frames)
            if int(trail_frames) != int(trail_hist_cap):
                trail_hist = deque(list(trail_hist)[-int(trail_frames):], maxlen=int(trail_frames))
                trail_hist_cap = int(trail_frames)

            out = surface_pool.get(W, H, pygame.SRCALPHA)
            hist_list = list(trail_hist)
            for idx, frm in enumerate(hist_list):
                age = (len(hist_list) - 1) - idx
                w = trail_alpha * (trail_decay ** float(age))
                if w <= 1e-6:
                    continue
                src = frm
                blur_k = int(trail_blur)
                if trail_blur_ramp and blur_k > 1:
                    blur_k = int(max(2, blur_k * (1 + age)))
                if blur_k and blur_k > 1:
                    bw = max(1, int(W / blur_k))
                    bh = max(1, int(H / blur_k))
                    src = pygame.transform.smoothscale(src, (bw, bh))
                    src = pygame.transform.smoothscale(src, (W, H))
                if trail_dim > 0:
                    dkey = (int(W), int(H), int(trail_dim))
                    if (trail_dim_cache is None) or (trail_dim_cache_key != dkey):
                        trail_dim_cache = pygame.Surface((W, H), pygame.SRCALPHA)
                        trail_dim_cache.fill((0, 0, 0, int(trail_dim)))
                        trail_dim_cache_key = dkey
                    src = src.copy()
                    src.blit(trail_dim_cache, (0, 0))
                else:
                    src = src.copy()
                src.set_alpha(int(255 * clamp(w, 0.0, 1.0)))
                if trail_blend == "add":
                    out.blit(src, (0, 0), special_flags=pygame.BLEND_RGBA_ADD)
                else:
                    out.blit(src, (0, 0))

            out.blit(display_frame_cur, (0, 0))
            display_frame = out
            trail_hist.append(display_frame_cur)
        else:
            display_frame = display_frame_cur
            if "trail_hist" in locals():
                try:
                    trail_hist.clear()
                except:
                    pass

            if "trail_dim_cache" in locals():
                trail_dim_cache = None
                trail_dim_cache_key = None

        if not record_headless:
            screen.blit(display_frame, (0, 0))

        if record_enabled:
            # Use new recorder backend if available
            if recorder:
                try:
                    # Fast path: avoid numpy conversion+transpose (very expensive)
                    if hasattr(recorder, "write_frame_bytes"):
                        frame_bytes = pygame.image.tostring(display_frame, "RGB")
                        recorder.write_frame_bytes(frame_bytes)
                    else:
                        # Fallback: Convert pygame surface to numpy array for recorder
                        frame_array = pygame.surfarray.array3d(display_frame)
                        # Transpose from (W, H, 3) to (H, W, 3)
                        frame_array = np.transpose(frame_array, (1, 0, 2))
                        # Write frame to recorder
                        recorder.write_frame(frame_array)
                except Exception as e:
                    if (not record_use_curses) or (not cui_ok) or (cui is None):
                        print(f"\r[Recording] Error writing frame: {e}", flush=True)
            else:
                # Fallback to old PNG saving (backward compatibility)
                try:
                    record_dir = getattr(args, "record_dir", None)
                    if record_dir:
                        os.makedirs(str(record_dir), exist_ok=True)
                        out_p = os.path.join(str(record_dir), f"frame_{record_frame_idx:06d}.png")
                        pygame.image.save(display_frame, out_p)
                except Exception as e:
                    if (not record_use_curses) or (not cui_ok) or (cui is None):
                        print(f"\r[Recording] Error saving PNG: {e}", flush=True)

            record_frame_idx += 1

            if record_headless:
                if record_use_curses and cui_ok and cui is not None:
                    cui_scroll = render_curses_ui(
                        cui,
                        curses_mod,
                        bool(cui_has_color),
                        int(cui_view),
                        int(cui_scroll),
                        t=float(t),
                        record_frame_idx=int(record_frame_idx),
                        record_start_time=float(record_start_time),
                        record_end_time=record_end_time,
                        record_fps=float(record_fps),
                        record_wall_t0=float(record_wall_t0),
                        record_curses_fps=float(record_curses_fps),
                        chart_end=float(chart_end),
                        judge=judge,
                        total_notes=int(total_notes),
                        particles_count=int(len(particles)),
                        note_times_by_kind=note_times_by_kind,
                        note_times_by_line_kind=note_times_by_line_kind,
                        approach=float(getattr(args, "approach", 3.0) or 3.0),
                        args=args,
                    )
                    last_cui_update_t = float(t)
                else:
                    try:
                        end_for_prog = float(record_end_time) if record_end_time is not None else float(chart_end)
                        denom = max(1e-6, float(end_for_prog) - float(record_start_time))
                        ratio = clamp((float(t) - float(record_start_time)) / denom, 0.0, 1.0)
                        msg = f"[record] {ratio*100:6.2f}%  frame={record_frame_idx:7d}  t={float(t):.3f}s"
                        print("\r" + msg + " " * 8, end="", flush=True)
                    except:
                        pass

                    if record_log_notes and record_log_interval > 1e-6 and (float(t) - float(last_record_log_t)) >= float(record_log_interval):
                        try:
                            total_past = 0
                            total_incoming = 0
                            for lid in note_times_by_line:
                                past, incoming = line_note_counts(
                                    note_times_by_line,
                                    int(lid),
                                    float(t),
                                    float(getattr(args, "approach", 3.0)),
                                )
                                total_past += int(past)
                                total_incoming += int(incoming)
                            seg_hint = ""
                            if lines:
                                try:
                                    ln0 = lines[0]
                                    seg_hint = (
                                        f" seg(rot)={track_seg_state(ln0.rot)}"
                                        f" seg(alpha)={track_seg_state(ln0.alpha)}"
                                        f" seg(scroll)={track_seg_state(ln0.scroll_px)}"
                                    )
                                except:
                                    seg_hint = ""
                            print(f"\n[record] past={int(total_past)} incoming={int(total_incoming)}{seg_hint}", flush=True)
                            last_record_log_t = float(t)
                        except:
                            last_record_log_t = float(t)

        if (not record_headless) and expand > 1.0:
            bw = W / expand
            bh = H / expand
            x0 = (W - bw) * 0.5
            y0 = (H - bh) * 0.5
            pygame.draw.rect(screen, (240, 240, 240), pygame.Rect(int(x0), int(y0), int(bw), int(bh)), 2)

        if not record_headless:
            now_ms = int(t * 1000.0)
            particles[:] = prune_particles(particles, now_ms)
            draw_particles(screen, particles, now_ms, W, H, expand)

        if (not record_headless) and line_text_draw_calls:
            line_text_draw_calls.sort(key=lambda x: x[0])
            for _pr, surf, x0, y0 in line_text_draw_calls:
                screen.blit(surf, (x0, y0))

        if record_headless:
            continue

        render_ui_overlay(
            screen,
            font=font,
            small=small,
            W=int(W),
            H=int(H),
            t=float(t),
            chart_end=float(chart_end),
            chart_info=chart_info,
            judge=judge,
            total_notes=int(total_notes),
            idx_next=int(idx_next),
            states_len=int(len(states)),
            lines_len=int(len(lines)),
            fmt=str(fmt),
            expand=float(expand),
            particles_count=int(len(particles)),
            note_render_count=int(note_render_count_last),
            hit_debug=bool(hit_debug),
            hit_debug_lines=hit_debug_lines,
            advance_active=bool(advance_active),
            start_time=getattr(args, "start_time", None),
            args=args,
            clock=clock,
        )

        pygame.display.flip()

    if record_use_curses and cui_ok and cui is not None:
        cleanup_curses_ui(cui)

    if _signal is not None and _old_sigint is not None:
        try:
            _signal.signal(_signal.SIGINT, _old_sigint)
        except:
            pass

    if record_headless and record_enabled:
        try:
            if not record_use_curses:
                print("\n[record] done", flush=True)
        except:
            pass

    audio.close()
    pygame.quit()
