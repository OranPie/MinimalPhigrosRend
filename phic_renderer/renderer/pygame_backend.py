from __future__ import annotations

import bisect
import logging
import math
import os
import time
from collections import deque
from typing import Any, Callable, Dict, List, Optional, Tuple

import pygame

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
from .pygame.textual_ui import init_textual_ui, RecordUISnapshot
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
from .pygame.pointer_input import PointerManager
from .pygame.manual_judgement import apply_manual_judgement
from .pygame.hold_logic import hold_finalize, hold_maintenance, hold_tick_fx
from .pygame.miss_logic import detect_misses
from .pygame.debug_judge_windows import draw_debug_judge_windows
from .pygame.trail_effect import apply_trail
from .pygame.frame_renderer import render_frame as render_frame_impl
from .pygame.record_writer import save_record_png, write_record_frame
from .pygame.post_ui import post_render_non_headless, post_render_record_headless_overlay
from .pygame.motion_blur import apply_motion_blur

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
    judge: Optional[Judge] = None,
    total_notes_override: Optional[int] = None,
    chart_end_override: Optional[float] = None,
    chart_info_override: Optional[Dict[str, Any]] = None,
    ui_time_offset: float = 0.0,
    stop_when_judged_cnt: Optional[int] = None,
    stop_when_hit_total: Optional[int] = None,
    should_stop_cb: Optional[Callable[[Dict[str, Any]], bool]] = None,
    reuse_pygame: bool = False,
    reuse_audio: bool = False,
    audio: Any = None,
):
    logger = logging.getLogger(__name__)
    if (not bool(reuse_pygame)) or (not pygame.get_init()):
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

    last_judge_event = None
    last_judge_events_frame: List[Dict[str, Any]] = []

    def _report_judge_event(ev: Dict[str, Any]):
        nonlocal last_judge_event, last_judge_events_frame
        try:
            last_judge_event = dict(ev or {})
            last_judge_events_frame.append(dict(ev or {}))
        except Exception:
            pass

        # Also feed the UI event panels, independent of hit_debug.
        try:
            if (tui_ok and tui is not None) or (record_use_curses and cui_ok and cui is not None):
                grade = str(ev.get("grade", ""))
                t_now = float(ev.get("t_now", 0.0))
                t_hit = float(ev.get("t_hit", 0.0))
                nid = int(ev.get("note_id", -1))
                dt_ms = (float(t_now) - float(t_hit)) * 1000.0
                _push_cui_event(f"{grade:7s} nid={nid:6d} dt={float(dt_ms):+7.1f}ms", t_now=float(t_now))
        except Exception:
            pass

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

    created_audio = False
    if audio is None:
        audio = create_audio_backend(getattr(args, "audio_backend", "pygame"))
        created_audio = True

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
        if bool(reuse_pygame):
            try:
                _surf = pygame.display.get_surface()
            except Exception:
                _surf = None
            if _surf is not None and tuple(getattr(_surf, "get_size")()) == (int(W), int(H)):
                screen = _surf
            else:
                screen = pygame.display.set_mode((W, H))
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

    # Apply start_time/end_time filtering for single charts.
    # Important: do NOT trim notes during recording; recording uses record_start_time to align timeline.
    if (not advance_active) and (not record_enabled):
        notes = filter_notes_by_time(notes, getattr(args, "start_time", None), getattr(args, "end_time", None))

    # Minimal simultaneous grouping
    group_simultaneous_notes(notes)

    # Total notes
    total_notes = compute_total_notes(notes, advance_active, advance_cfg, W, H)
    if total_notes_override is not None:
        try:
            total_notes = int(total_notes_override)
        except Exception:
            pass

    # chart end
    chart_end = compute_chart_end(notes, advance_active, getattr(args, "end_time", None) if (not advance_active) else None)
    if chart_end_override is not None:
        try:
            chart_end = float(chart_end_override)
        except Exception:
            pass

    note_times_by_line = compute_note_times_by_line(notes)
    note_times_by_line_kind, note_times_by_kind = compute_note_times_by_line_kind(notes)

    hold_fx_interval_ms = max(10, int(getattr(args, "hold_fx_interval_ms", 200)))
    hold_tail_tol = clamp(float(getattr(args, "hold_tail_tol", 0.8)), 0.0, 1.0)
    hitsound_min_interval_ms = max(0, int(getattr(args, "hitsound_min_interval_ms", 30)))

    hitsound = HitsoundPlayer(audio=audio, chart_dir=chart_dir, min_interval_ms=hitsound_min_interval_ms)

    # Note states
    states = [NoteState(n) for n in notes]
    idx_next = 0

    if judge is None:
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
    cui_events_incoming: List[str] = []
    cui_events_past: deque = deque(maxlen=256)

    def _push_cui_event(msg: str, *, t_now: float):
        try:
            s = f"{float(t_now):9.3f}s  {str(msg)}"
            cui_events_incoming.append(s)
            cui_events_past.appendleft(s)
        except:
            pass

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

    tui_ok = False
    tui = None

    # input
    try:
        _flick_thr_ratio = float(getattr(args, "flick_threshold", 0.02))
    except Exception:
        _flick_thr_ratio = 0.02
    pointers = PointerManager(int(W), int(H), float(_flick_thr_ratio))

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
    tui_frame_step = 1
    if record_enabled and record_fps > 1e-6:
        try:
            tui_frame_step = max(1, int(round(float(record_fps) / max(1e-6, float(record_curses_fps)))))
        except Exception:
            tui_frame_step = 1
    if record_headless and bool(getattr(args, "record_use_textual", False)):
        tui = getattr(args, "textual_ui", None)
        tui_ok = tui is not None
        if (not tui_ok) and (not bool(getattr(args, "no_curses", False))):
            record_use_curses = True

    if record_use_curses and (not tui_ok):
        cui_ok, cui, curses_mod, cui_has_color = init_curses_ui()
        if not cui_ok:
            record_use_curses = False
            cui = None
    while running:
        # Clear per-frame transform cache
        transform_cache.next_frame()

        # On macOS, SDL/Cocoa event pumping must be done on the main thread.
        # In headless recording with Textual UI, renderer may run in a worker thread;
        # skip pygame event pumping entirely and rely on Textual state for quit/selection.
        skip_pygame_events = bool(record_headless and tui_ok and tui is not None)

        if skip_pygame_events:
            try:
                if bool(getattr(tui.state, "should_quit", False)):
                    running = False
                    break
            except Exception:
                pass

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
            evs = []
        else:
            evs = ([] if skip_pygame_events else pygame.event.get())

        try:
            cui_events_incoming.clear()
        except:
            cui_events_incoming = []

        if record_use_curses and (not tui_ok) and cui_ok and cui is not None:
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
        pointers.begin_frame()

        for ev in evs:
            try:
                pointers.process_event(ev)
            except Exception:
                pass
            try:
                if (tui_ok and tui is not None) or (record_use_curses and cui_ok and cui is not None):
                    _push_cui_event(f"pygame ev={getattr(ev, 'type', None)}", t_now=float((now_sec() - t0) * float(getattr(args, 'chart_speed', 1.0))))
            except:
                pass
            if ev.type == pygame.QUIT:
                running = False
            elif ev.type == pygame.KEYDOWN:
                if ev.key == pygame.K_ESCAPE:
                    running = False
                elif ev.key == pygame.K_SPACE:
                    pointers.set_keyboard_down(True)
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
                    pointers.set_keyboard_down(False)

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

            base, line_text_draw_calls, note_render_count, last_debug_ms_new, bg_scaled_cache_key_new, bg_scaled_cache_new, dim_surf_cache_key_new, dim_surf_cache_new = render_frame_impl(
                t_draw=float(t_draw),
                args=args,
                state_mod=state,
                RW=int(RW),
                RH=int(RH),
                W=int(W),
                H=int(H),
                expand=float(expand),
                overrender=float(overrender),
                surface_pool=surface_pool,
                transform_cache=transform_cache,
                bg_blurred=bg_blurred,
                bg_dim_alpha=bg_dim_alpha,
                bg_scaled_cache_key=bg_scaled_cache_key,
                bg_scaled_cache=bg_scaled_cache,
                dim_surf_cache_key=dim_surf_cache_key,
                dim_surf_cache=dim_surf_cache,
                lines=lines,
                states=states,
                idx_next=int(idx_next),
                base_note_w=int(base_note_w),
                base_note_h=int(base_note_h),
                note_scale_x=float(note_scale_x),
                note_scale_y=float(note_scale_y),
                hold_body_w=int(hold_body_w),
                outline_w=int(outline_w),
                line_w=int(line_w),
                dot_r=int(dot_r),
                line_len=int(line_len),
                chart_dir=str(chart_dir),
                line_tex_cache=line_tex_cache,
                small=small,
                note_dbg_cache=note_dbg_cache,
                last_debug_ms=int(last_debug_ms),
                line_last_hit_ms=line_last_hit_ms,
                respack=respack,
                hitfx=hitfx,
                bad_ghosts=bad_ghosts,
                MISS_FADE_SEC=float(MISS_FADE_SEC),
                BAD_GHOST_SEC=float(BAD_GHOST_SEC),
            )

            last_debug_ms = int(last_debug_ms_new)
            bg_scaled_cache_key = bg_scaled_cache_key_new
            bg_scaled_cache = bg_scaled_cache_new
            dim_surf_cache_key = dim_surf_cache_key_new
            dim_surf_cache = dim_surf_cache_new

            note_render_count_last = int(note_render_count)
            return base, line_text_draw_calls

        if "line_last_hit_ms" not in locals():
            line_last_hit_ms: Dict[int, int] = {}

        def _mark_line_hit(lid: int, now_ms: int):
            line_last_hit_ms[lid] = int(now_ms)

        def _push_hit_debug(
            *,
            t_now: float,
            t_hit: float,
            note_id: int,
            judgement: str,
            hold_percent: Optional[float] = None,
            note_kind: Optional[int] = None,
            mh: Optional[bool] = None,
            line_id: Optional[int] = None,
            source: Optional[str] = None,
        ):
            nonlocal hit_debug_seq
            if not hit_debug:
                # still report judge event for playlist even if debug overlay is disabled
                try:
                    _report_judge_event(
                        {
                            "grade": str(judgement),
                            "t_now": float(t_now),
                            "t_hit": float(t_hit),
                            "note_id": int(note_id),
                            "note_kind": (int(note_kind) if note_kind is not None else None),
                            "mh": (bool(mh) if mh is not None else None),
                            "line_id": (int(line_id) if line_id is not None else None),
                            "source": (str(source) if source is not None else None),
                            "hold_percent": (float(hold_percent) if hold_percent is not None else None),
                        }
                    )
                except Exception:
                    pass
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
            try:
                if hp is None:
                    _push_cui_event(f"{str(judgement):7s} nid={int(note_id):6d} dt={float(dt_ms):+7.1f}ms", t_now=float(t_now))
                else:
                    _push_cui_event(f"{str(judgement):7s} nid={int(note_id):6d} dt={float(dt_ms):+7.1f}ms hold={float(hp)*100:5.1f}%", t_now=float(t_now))
            except:
                pass

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
                            _push_hit_debug(
                                t_now=float(t_hit),
                                t_hit=float(n.t_hit),
                                note_id=int(getattr(n, "nid", -1)),
                                judgement="MISS",
                                note_kind=int(getattr(n, "kind", 0) or 0),
                                mh=bool(getattr(n, "mh", False)),
                                line_id=int(getattr(n, "line_id", -1)),
                                source="autoplay",
                            )
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
                        _push_hit_debug(
                            t_now=float(t_fx),
                            t_hit=float(n.t_hit),
                            note_id=int(getattr(n, "nid", -1)),
                            judgement=str(grade),
                            note_kind=int(getattr(n, "kind", 0) or 0),
                            mh=bool(getattr(n, "mh", False)),
                            line_id=int(getattr(n, "line_id", -1)),
                            source="autoplay",
                        )
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
                        _push_hit_debug(
                            t_now=float(t_hit),
                            t_hit=float(n.t_hit),
                            note_id=int(getattr(n, "nid", -1)),
                            judgement="MISS",
                            hold_percent=None,
                            note_kind=int(getattr(n, "kind", 0) or 0),
                            mh=bool(getattr(n, "mh", False)),
                            line_id=int(getattr(n, "line_id", -1)),
                            source="autoplay_hold",
                        )
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
                        _push_hit_debug(
                            t_now=float(t_fx),
                            t_hit=float(n.t_hit),
                            note_id=int(getattr(n, "nid", -1)),
                            judgement=str(grade),
                            hold_percent=0.0,
                            note_kind=int(getattr(n, "kind", 0) or 0),
                            mh=bool(getattr(n, "mh", False)),
                            line_id=int(getattr(n, "line_id", -1)),
                            source="autoplay_hold",
                        )
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
            for pf in pointers.frame_pointers():
                try:
                    apply_manual_judgement(
                        args=args,
                        t=float(t),
                        W=int(W),
                        lines=lines,
                        states=states,
                        idx_next=int(idx_next),
                        judge=judge,
                        record_enabled=bool(record_enabled),
                        respack=respack,
                        hitsound=hitsound,
                        hitfx=hitfx,
                        particles=particles,
                        HitFX_cls=HitFX,
                        ParticleBurst_cls=ParticleBurst,
                        hold_fx_interval_ms=int(hold_fx_interval_ms),
                        mark_line_hit_cb=_mark_line_hit,
                        push_hit_debug_cb=_push_hit_debug,
                        pointer_id=int(pf.pointer_id),
                        pointer_x=pf.x,
                        gesture=pf.gesture,
                        hold_like_down=bool(pf.down),
                        press_edge=bool(pf.press_edge),
                    )
                except Exception:
                    pass

        # hold maintenance
        if not getattr(args, "autoplay", False):
            try:
                hold_maintenance(
                    states=states,
                    idx_next=int(idx_next),
                    t=float(t),
                    hold_tail_tol=float(hold_tail_tol),
                    pointers=pointers,
                    judge=judge,
                )
            except Exception:
                pass

        # hold finalize
        try:
            hold_finalize(
                states=states,
                idx_next=int(idx_next),
                t=float(t),
                hold_tail_tol=float(hold_tail_tol),
                miss_window=float(MISS_WINDOW),
                judge=judge,
                push_hit_debug_cb=_push_hit_debug,
            )
        except Exception:
            pass

        # hold tick fx
        try:
            hold_tick_fx(
                states=states,
                idx_next=int(idx_next),
                t=float(t),
                hold_fx_interval_ms=int(hold_fx_interval_ms),
                lines=lines,
                respack=respack,
                hitfx=hitfx,
                particles=particles,
                HitFX_cls=HitFX,
                ParticleBurst_cls=ParticleBurst,
                mark_line_hit_cb=_mark_line_hit,
            )
        except Exception:
            pass

        # miss detection
        try:
            detect_misses(
                states=states,
                idx_next=int(idx_next),
                t=float(t),
                miss_window=float(MISS_WINDOW),
                judge=judge,
                report_event_cb=_report_judge_event,
            )
        except Exception:
            pass

        while idx_next < len(states) and states[idx_next].judged:
            idx_next += 1

        stop_hit = False
        stop_judged = False
        if stop_when_hit_total is not None:
            try:
                stop_hit = int(getattr(judge, "hit_total", 0)) >= int(stop_when_hit_total)
            except Exception:
                stop_hit = False
        if stop_when_judged_cnt is not None:
            try:
                stop_judged = int(getattr(judge, "judged_cnt", 0)) >= int(stop_when_judged_cnt)
            except Exception:
                stop_judged = False

        if stop_hit or stop_judged:
            running = False

        if should_stop_cb is not None:
            try:
                if bool(
                    should_stop_cb(
                        {
                            "t": float(t),
                            "idx_next": int(idx_next),
                            "total_notes": int(total_notes),
                            "chart_end": float(chart_end),
                            "judge": judge,
                            "chart_info": (chart_info_override if chart_info_override is not None else chart_info),
                            "record_enabled": bool(record_enabled),
                            "record_headless": bool(record_headless),
                            "last_judge_event": last_judge_event,
                            "judge_events_frame": list(last_judge_events_frame),
                        }
                    )
                ):
                    running = False
            except Exception:
                pass

        try:
            last_judge_events_frame.clear()
        except Exception:
            last_judge_events_frame = []

        display_frame, line_text_draw_calls = _render_frame(t)

        # Debug: judge windows (draw judge area for each note)
        if getattr(args, "debug_judge_windows", False):
            try:
                draw_debug_judge_windows(
                    display_frame=display_frame,
                    args=args,
                    t=float(t),
                    W=int(W),
                    H=int(H),
                    overrender=float(overrender),
                    expand=float(expand),
                    lines=lines,
                    states=states,
                    idx_next=int(idx_next),
                    RW=int(RW),
                    RH=int(RH),
                )
            except Exception:
                pass

        t_ui = float(t) + float(ui_time_offset or 0.0)
        chart_end_ui = float(chart_end)
        if chart_end_override is not None:
            try:
                chart_end_ui = float(chart_end_override)
            except Exception:
                chart_end_ui = float(chart_end)

        if record_headless and record_enabled:
            try:
                post_render_record_headless_overlay(
                    args=args,
                    display_frame=display_frame,
                    W=int(W),
                    H=int(H),
                    t=float(t_ui),
                    chart_end=float(chart_end_ui),
                    fmt=str(fmt),
                    idx_next=int(idx_next),
                    states_len=int(len(states)),
                    lines_len=int(len(lines)),
                    total_notes=int(total_notes),
                    judge=judge,
                    particles=particles,
                    line_text_draw_calls=line_text_draw_calls,
                    font=font,
                    small=small,
                    expand=float(expand),
                    advance_active=bool(advance_active),
                    hit_debug=bool(hit_debug),
                    hit_debug_lines=hit_debug_lines,
                )
            except Exception:
                pass
        if mb_samples > 1 and mb_shutter > 1e-6:
            try:
                display_frame_cur = apply_motion_blur(
                    t=float(t),
                    dt_frame=float(_dt_frame),
                    chart_speed=float(chart_speed),
                    mb_samples=int(mb_samples),
                    mb_shutter=float(mb_shutter),
                    W=int(W),
                    H=int(H),
                    render_frame_cb=_render_frame,
                    surface_pool=surface_pool,
                )
            except Exception:
                display_frame_cur = pygame.transform.smoothscale(display_frame, (W, H))
        else:
            display_frame_cur = pygame.transform.smoothscale(display_frame, (W, H))
        surface_pool.release(display_frame)

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

        try:
            display_frame, trail_hist, trail_hist_cap, trail_dim_cache, trail_dim_cache_key = apply_trail(
                surface_pool=surface_pool,
                W=int(W),
                H=int(H),
                display_frame_cur=display_frame_cur,
                trail_alpha=float(trail_alpha),
                trail_frames=int(trail_frames),
                trail_decay=float(trail_decay),
                trail_blur=int(trail_blur),
                trail_blur_ramp=bool(trail_blur_ramp),
                trail_dim=int(trail_dim),
                trail_blend=str(trail_blend),
                trail_hist=locals().get("trail_hist", None),
                trail_hist_cap=int(locals().get("trail_hist_cap", int(trail_frames))),
                trail_dim_cache=trail_dim_cache,
                trail_dim_cache_key=trail_dim_cache_key,
            )
        except Exception:
            display_frame = display_frame_cur

        if not record_headless:
            screen.blit(display_frame, (0, 0))

        if record_enabled:
            # Use new recorder backend if available
            if recorder:
                try:
                    write_record_frame(
                        recorder=recorder,
                        display_frame=display_frame,
                        record_use_curses=bool(record_use_curses),
                        cui_ok=bool(cui_ok),
                        cui=cui,
                    )
                except Exception:
                    pass
            else:
                # Fallback to old PNG saving (backward compatibility)
                try:
                    save_record_png(
                        display_frame=display_frame,
                        record_dir=getattr(args, "record_dir", None),
                        record_frame_idx=int(record_frame_idx),
                        record_use_curses=bool(record_use_curses),
                        cui_ok=bool(cui_ok),
                        cui=cui,
                    )
                except Exception:
                    pass

            record_frame_idx += 1

            if record_headless:
                if tui_ok and tui is not None:
                    try:
                        if bool(getattr(tui.state, 'should_quit', False)):
                            running = False
                        else:
                            if (int(record_frame_idx) % int(tui_frame_step)) == 0:
                                lids = sorted(note_times_by_line_kind.keys())
                                sel_idx = int(getattr(tui.state, 'selected_line', 0) or 0)
                                if sel_idx < 0:
                                    sel_idx = 0
                                if sel_idx > max(0, len(lids) - 1):
                                    sel_idx = max(0, len(lids) - 1)
                                try:
                                    tui.state.selected_line = int(sel_idx)
                                except Exception:
                                    pass

                                line_props: List[str] = []
                                note_lines: List[str] = []
                                if lids:
                                    lid = int(lids[sel_idx])
                                    approach_t = float(getattr(args, 'approach', 3.0) or 3.0)
                                    past4, inc4 = line_note_counts_kind(note_times_by_line_kind, int(lid), float(t), float(approach_t))
                                    try:
                                        past_all, inc_all = line_note_counts(note_times_by_kind, float(t), float(approach_t))
                                        line_props.append(f"ALL  P {past_all[0]}/{past_all[1]}/{past_all[2]}/{past_all[3]}   I {inc_all[0]}/{inc_all[1]}/{inc_all[2]}/{inc_all[3]}")
                                    except Exception:
                                        pass
                                    line_props.append(f"selected L{lid:02d}  idx={sel_idx+1}/{max(1, len(lids))}")
                                    line_props.append(f"P {past4[0]}/{past4[1]}/{past4[2]}/{past4[3]}   I {inc4[0]}/{inc4[1]}/{inc4[2]}/{inc4[3]}")
                                    try:
                                        lx, ly, lr, la01, lsc, laraw = eval_line_state(lines[int(lid)], float(t))
                                        line_props.append(f"pos=({lx:7.1f},{ly:7.1f})  rot={lr:+7.3f}  alpha01={la01:4.2f} raw={laraw:+6.3f}")
                                        line_props.append(f"scroll={lsc:10.2f}")
                                        try:
                                            tr_rot = track_seg_state(getattr(lines[int(lid)], 'rot', None))
                                            tr_alp = track_seg_state(getattr(lines[int(lid)], 'alpha', None))
                                            tr_scr = track_seg_state(getattr(lines[int(lid)], 'scroll_px', None))
                                            line_props.append(f"seg rot={tr_rot}  a={tr_alp}  scr={tr_scr}")
                                        except Exception:
                                            pass
                                    except Exception:
                                        line_props.append("(line state unavailable)")
                                else:
                                    line_props.append("no lines")

                                try:
                                    margin = 120
                                    if idx_next is None:
                                        start_i = 0
                                    else:
                                        start_i = max(0, int(idx_next) - 64)
                                    end_i = min(len(notes), start_i + 512)
                                    shown = 0
                                    for i in range(start_i, end_i):
                                        if shown >= 48:
                                            break
                                        n = notes[i]
                                        s = states[i] if i < len(states) else None
                                        try:
                                            t_enter = float(getattr(n, 't_enter', -1e9))
                                        except Exception:
                                            t_enter = -1e9
                                        if float(t) < float(t_enter):
                                            continue

                                        lid = int(getattr(n, 'line_id', 0) or 0)
                                        if lid < 0 or lid >= len(lines):
                                            continue
                                        try:
                                            lx, ly, lr, la01, lsc, laraw = eval_line_state(lines[lid], float(t))
                                        except Exception:
                                            continue

                                        kind = int(getattr(n, 'kind', 0) or 0)
                                        above = bool(getattr(n, 'above', True))
                                        nid = int(getattr(n, 'nid', i))
                                        hit = bool(getattr(s, 'hit', False)) if s is not None else False
                                        holding = bool(getattr(s, 'holding', False)) if s is not None else False
                                        miss = bool(getattr(s, 'miss', False)) if s is not None else False
                                        flg = ("H" if hit else "-") + ("h" if holding else "-") + ("M" if miss else "-")

                                        if kind == 3:
                                            sh = float(getattr(n, 'scroll_hit', 0.0) or 0.0)
                                            se = float(getattr(n, 'scroll_end', 0.0) or 0.0)
                                            if hit or holding or (float(t) >= float(getattr(n, 't_hit', 0.0) or 0.0)):
                                                head_target_scroll = sh if float(lsc) <= float(sh) else float(lsc)
                                            else:
                                                head_target_scroll = float(sh)
                                            hx, hy = note_world_pos(float(lx), float(ly), float(lr), float(lsc), n, float(head_target_scroll), False)
                                            tx, ty = note_world_pos(float(lx), float(ly), float(lr), float(lsc), n, float(se), True)
                                            minx = min(float(hx), float(tx))
                                            maxx = max(float(hx), float(tx))
                                            miny = min(float(hy), float(ty))
                                            maxy = max(float(hy), float(ty))
                                            if maxx < -margin or minx > float(W + margin) or maxy < -margin or miny > float(H + margin):
                                                continue
                                            note_lines.append(f"#{i:05d} nid={nid:6d} HOLD L{lid:02d} {'A' if above else 'B'} {flg} head=({hx:7.1f},{hy:7.1f}) tail=({tx:7.1f},{ty:7.1f})")
                                            shown += 1
                                        else:
                                            sh = float(getattr(n, 'scroll_hit', 0.0) or 0.0)
                                            x, y = note_world_pos(float(lx), float(ly), float(lr), float(lsc), n, float(sh), False)
                                            ws = float(base_note_w) * float(getattr(n, 'size_px', 1.0) or 1.0)
                                            hs = float(base_note_h) * float(getattr(n, 'size_px', 1.0) or 1.0)
                                            if (float(x) + ws / 2 < -margin) or (float(x) - ws / 2 > float(W + margin)) or (float(y) + hs / 2 < -margin) or (float(y) - hs / 2 > float(H + margin)):
                                                continue
                                            kd = {1: 'TAP', 2: 'DRG', 4: 'FLK'}.get(kind, str(kind))
                                            note_lines.append(f"#{i:05d} nid={nid:6d} {kd:3s}  L{lid:02d} {'A' if above else 'B'} {flg} pos=({x:7.1f},{y:7.1f})")
                                            shown += 1
                                except Exception:
                                    note_lines = ["(notes unavailable)"]

                                snap = RecordUISnapshot(
                                    header_lines=(
                                        (lambda: (
                                            [
                                                f"t={float(t):9.3f}s  frame={int(record_frame_idx):8d}  fps={float(record_fps):5.1f}",
                                                f"start={float(record_start_time):9.3f}s  end={(float(record_end_time) if record_end_time is not None else float(chart_end)):9.3f}s  chart_end={float(chart_end):9.3f}s",
                                                f"combo={int(getattr(judge, 'combo', 0) or 0):5d}  judged={int(getattr(judge, 'judged_cnt', 0) or 0):6d}  acc={(float(getattr(judge, 'acc_sum', 0.0) or 0.0) / max(1, int(getattr(judge, 'judged_cnt', 0) or 0))):.4f}",
                                            ]
                                        ))()
                                    ),
                                    progress01=(
                                        (lambda: (
                                            0.0
                                            if (float((record_end_time if record_end_time is not None else chart_end) or 0.0) - float(record_start_time)) <= 1e-6
                                            else (float(t) - float(record_start_time)) / float((record_end_time if record_end_time is not None else chart_end) - float(record_start_time))
                                        ))()
                                    ),
                                    incoming=list(cui_events_incoming)[:80],
                                    past=list(cui_events_past)[:200],
                                    line_props=line_props[:40],
                                    notes=note_lines[:120],
                                    selected_line=int(sel_idx),
                                    lines_total=int(len(lids)),
                                )
                                tui.push(snap)
                    except Exception:
                        pass

                elif record_use_curses and cui_ok and cui is not None:
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
                        events_incoming=list(cui_events_incoming),
                        events_past=list(cui_events_past),
                        lines=lines,
                        notes=notes,
                        states=states,
                        idx_next=int(idx_next),
                        W=int(W),
                        H=int(H),
                    )
                    last_cui_update_t = float(t)
                else:
                    try:
                        print_recording_progress(
                            float(t),
                            int(record_frame_idx),
                            float(record_start_time),
                            record_end_time,
                            float(chart_end),
                        )
                    except:
                        pass

                    if record_log_notes and record_log_interval > 1e-6 and (float(t) - float(last_record_log_t)) >= float(record_log_interval):
                        try:
                            print_recording_notes(
                                float(t),
                                note_times_by_line,
                                lines,
                                float(getattr(args, "approach", 3.0)),
                            )
                            last_record_log_t = float(t)
                        except:
                            last_record_log_t = float(t)

        if not record_headless:
            try:
                post_render_non_headless(
                    screen=screen,
                    W=int(W),
                    H=int(H),
                    expand=float(expand),
                    t=float(t),
                    particles=particles,
                    line_text_draw_calls=line_text_draw_calls,
                )
            except Exception:
                pass

        if record_headless:
            continue

        render_ui_overlay(
            screen,
            font=font,
            small=small,
            W=int(W),
            H=int(H),
            t=float(t_ui),
            chart_end=float(chart_end_ui),
            chart_info=(chart_info_override if chart_info_override is not None else chart_info),
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

    # Textual UI is owned by the main thread (record.py). Do not stop it here.

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

    try:
        if created_audio and (not bool(reuse_audio)):
            audio.close()
    except Exception:
        pass

    if not bool(reuse_pygame):
        pygame.quit()
