from __future__ import annotations

import json
import math
import time
import argparse
import sys
import zipfile
import tempfile
import os
import shutil
from typing import Any, Dict, List, Optional, Tuple

import bisect
import pygame

from . import state
from . import easing
from .util import (
    clamp, lerp, now_sec, hsv_to_rgb,
    rotate_vec, rect_corners,
    apply_expand_xy, apply_expand_pts,
    draw_poly_rgba, draw_poly_outline_rgba,
    draw_line_rgba, draw_ring,
)
from .tracks import EasedSeg, PiecewiseEased, SumTrack, Seg1D, IntegralTrack
from .types import RuntimeNote, RuntimeLine, NoteState
from .effects import HitFX, ParticleBurst
from .respack import Respack, load_respack
from .chart_pack import ChartPack, load_chart_pack
from .chart_loader import load_chart
from .judge import Judge, JUDGE_WEIGHT
from .kinematics import eval_line_state, note_world_pos
from .render import NOTE_TYPE_COLORS, tint, draw_hold_3slice
from .visibility import precompute_t_enter
from .timewarp import _TimeWarpEval, _TimeWarpIntegral

# Global respack instance (kept for backward-compat with the original single-file code)
respack: Optional[Respack] = None

def main():
    global respack  # Declare we're using the global variable

    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=False, default=None, help="chart.json OR chart pack folder OR .zip/.pez pack")
    ap.add_argument("--advance", type=str, default=None)
    ap.add_argument("--config", type=str, default=None)
    ap.add_argument("--save_config", type=str, default=None)
    ap.add_argument("--w", type=int, default=1280)
    ap.add_argument("--h", type=int, default=720)
    ap.add_argument("--approach", type=float, default=3.0, help="Seconds ahead to draw")
    ap.add_argument("--chart_speed", type=float, default=1.0)
    ap.add_argument("--autoplay", action="store_true")
    ap.add_argument("--no_cull", action="store_true", help="Draw all notes (can be slow)")
    ap.add_argument("--respack", type=str, default=None, help="Respack zip path")
    ap.add_argument("--bg", type=str, default=None, help="Background image path")
    ap.add_argument("--bg_blur", type=int, default=10, help="Blur strength (downscale factor)")
    ap.add_argument("--bg_dim", type=int, default=120, help="Dark overlay alpha 0..255")
    ap.add_argument("--bgm", type=str, default=None, help="BGM audio file path (ogg/mp3/wav)")
    ap.add_argument("--bgm_volume", type=float, default=0.8)
    ap.add_argument("--note_scale_x", type=float, default=1.0)
    ap.add_argument("--note_scale_y", type=float, default=1.0)
    ap.add_argument("--hold_fx_interval_ms", type=int, default=200)
    ap.add_argument("--hold_tail_tol", type=float, default=0.8)
    ap.add_argument("--hitfx_scale_mul", type=float, default=1.0)
    ap.add_argument("--multicolor_lines", action="store_true")
    ap.add_argument("--no_note_outline", action="store_true")
    ap.add_argument("--line_alpha_affects_notes", type=str, default="negative_only", choices=["never", "negative_only", "always"])
    ap.add_argument("--expand", type=float, default=1.0)
    ap.add_argument("--rpe_easing_shift", type=int, default=0)
    ap.add_argument("--debug_line_label", action="store_true")
    ap.add_argument("--debug_line_stats", action="store_true")
    ap.add_argument("--debug_judge_windows", action="store_true")
    ap.add_argument("--debug_note_info", action="store_true")
    ap.add_argument("--debug_particles", action="store_true")
    ap.add_argument("--hitsound_min_interval_ms", type=int, default=30)
    ap.add_argument("--no_title_overlay", action="store_true")
    ap.add_argument("--font_path", type=str, default=None)
    ap.add_argument("--start_time", type=float, default=None, help="Start time in seconds (cuts chart before this time)")
    ap.add_argument("--end_time", type=float, default=None, help="End time in seconds (cuts chart after this time)")
    args = ap.parse_args()

    if (not args.input) and (not args.advance):
        raise SystemExit("Either --input or --advance must be provided")

    if args.config:
        try:
            with open(args.config, "r", encoding="utf-8") as f:
                cfg = json.load(f)
            for k, v in (cfg or {}).items():
                if not hasattr(args, k):
                    continue
                if ("--" + k) in sys.argv:
                    continue
                setattr(args, k, v)
        except:
            pass

    if args.save_config:
        out_cfg = {
            "w": args.w,
            "h": args.h,
            "approach": args.approach,
            "chart_speed": args.chart_speed,
            "autoplay": bool(args.autoplay),
            "no_cull": bool(args.no_cull),
            "respack": args.respack,
            "bg": args.bg,
            "bg_blur": args.bg_blur,
            "bg_dim": args.bg_dim,
            "bgm": args.bgm,
            "bgm_volume": args.bgm_volume,
            "note_scale_x": args.note_scale_x,
            "note_scale_y": args.note_scale_y,
            "hold_fx_interval_ms": args.hold_fx_interval_ms,
            "hold_tail_tol": args.hold_tail_tol,
            "hitfx_scale_mul": args.hitfx_scale_mul,
            "multicolor_lines": bool(args.multicolor_lines),
            "no_note_outline": bool(args.no_note_outline),
            "line_alpha_affects_notes": args.line_alpha_affects_notes,
            "debug_line_label": bool(args.debug_line_label),
            "debug_line_stats": bool(args.debug_line_stats),
            "debug_judge_windows": bool(args.debug_judge_windows),
            "debug_note_info": bool(args.debug_note_info),
            "debug_particles": bool(args.debug_particles),
            "hitsound_min_interval_ms": args.hitsound_min_interval_ms,
            "expand": args.expand,
            "no_title_overlay": bool(args.no_title_overlay),
            "rpe_easing_shift": args.rpe_easing_shift,
            "font_path": args.font_path,
            "start_time": args.start_time,
            "end_time": args.end_time,
        }
        try:
            with open(args.save_config, "w", encoding="utf-8") as f:
                json.dump(out_cfg, f, ensure_ascii=False, indent=2)
        except:
            pass

    W, H = args.w, args.h
    expand = float(args.expand) if args.expand is not None else 1.0
    if expand <= 1.000001:
        expand = 1.0

    # Visibility checks (shared across modules)
    state.expand_factor = expand

    # RPE easingType shift (some exporters are 1-based)
    easing.set_rpe_easing_shift(int(args.rpe_easing_shift))

    try:
        pygame.mixer.pre_init(44100, -16, 2, 512)
    except:
        pass
    pygame.init()
    try:
        pygame.mixer.init()
        pygame.mixer.set_num_channels(32)
    except:
        pass
    screen = pygame.display.set_mode((W, H))
    pygame.display.set_caption("Mini Phigros Renderer (Official + RPE, rot/alpha/color)")
    clock = pygame.time.Clock()
    font = None
    small = None
    if args.font_path and os.path.exists(str(args.font_path)):
        try:
            font = pygame.font.Font(str(args.font_path), 22)
            small = pygame.font.Font(str(args.font_path), 16)
        except:
            font = None
            small = None
    if font is None or small is None:
        if os.path.exists("cmdysj.ttf"):
            try:
                font = pygame.font.Font("cmdysj.ttf", 22)
                small = pygame.font.Font("cmdysj.ttf", 16)
            except:
                font = None
                small = None
    if font is None or small is None:
        font = pygame.font.SysFont("consolas", 22)
        small = pygame.font.SysFont("consolas", 16)

    # Load respack (set global variable)
    respack = load_respack(args.respack) if args.respack else None
    state.respack = respack

    # Load chart pack or direct json (or advance mode)
    pack = None
    chart_path = args.input
    music_path = None
    bg_path = None
    chart_info = {}   # for UI display (name/level/difficulty)
    bg_dim_alpha = None

    advance_cfg = None
    advance_active = False
    advance_mix = False
    advance_tracks_bgm: List[Dict[str, Any]] = []
    advance_main_bgm: Optional[str] = None
    advance_bgm_active = False
    advance_segment_idx = 0
    advance_segment_starts: List[float] = []
    advance_segment_bgm: List[Optional[str]] = []

    packs_keepalive: List[ChartPack] = []

    if args.advance:
        with open(str(args.advance), "r", encoding="utf-8") as f:
            advance_cfg = json.load(f) or {}
        advance_active = True
        advance_mix = bool(advance_cfg.get("mix", False))

        advance_base_dir = os.path.dirname(os.path.abspath(str(args.advance)))

        mode = str(advance_cfg.get("mode", "sequence"))
        all_lines: List[RuntimeLine] = []
        all_notes: List[RuntimeNote] = []
        lid_base = 0

        def _load_one_input(inp: str) -> Tuple[str, float, List[RuntimeLine], List[RuntimeNote], Optional[str], Optional[str], Dict[str, Any], str, str]:
            p = None
            chart_p = inp
            music_p = None
            bg_p = None
            info = {}
            if os.path.isdir(inp) or (os.path.isfile(inp) and str(inp).lower().endswith((".zip", ".pez"))):
                p = load_chart_pack(inp)
                packs_keepalive.append(p)
                chart_p = p.chart_path
                music_p = p.music_path
                bg_p = p.bg_path
                info = p.info
            fmt_i, off_i, lines_i, notes_i = load_chart(chart_p, W, H)
            base_dir = os.path.dirname(os.path.abspath(chart_p))
            return fmt_i, off_i, lines_i, notes_i, music_p, bg_p, info, chart_p, base_dir

        if mode == "sequence":
            items = list(advance_cfg.get("items", []) or [])
            cur_start = 0.0
            for it in items:
                inp = str(it.get("input"))
                start_local = float(it.get("start", 0.0))
                end_local = it.get("end", None)
                end_local = float(end_local) if end_local is not None else 1e18
                speed = float(it.get("chart_speed", 1.0))
                start_at = float(it.get("start_at", cur_start))
                fmt_i, off_i, lines_i, notes_i, music_p, bg_p, info, chart_p, base_dir = _load_one_input(inp)

                # choose bg/bgm from item if provided
                seg_bgm = str(it.get("bgm")) if it.get("bgm") else (music_p if (music_p and os.path.exists(music_p)) else None)
                if seg_bgm and (not os.path.isabs(seg_bgm)):
                    seg_bgm = os.path.join(base_dir, seg_bgm)
                seg_bg = str(it.get("bg")) if it.get("bg") else (bg_p if (bg_p and os.path.exists(bg_p)) else None)
                if seg_bg and (not os.path.isabs(seg_bg)):
                    seg_bg = os.path.join(base_dir, seg_bg)
                if seg_bg and (not bg_path):
                    bg_path = seg_bg
                    chart_info = info or {}
                    bd = chart_info.get("backgroundDim", None)
                    if bd is not None:
                        bg_dim_alpha = int(clamp(float(bd), 0.0, 1.0) * 255)

                # time_offset makes local_t(start_at)==start_local
                time_offset = float(it.get("time_offset", 0.0)) + start_local + off_i

                # warp lines
                lid_map: Dict[int, int] = {}
                for ln in lines_i:
                    new_lid = lid_base
                    lid_base += 1
                    lid_map[ln.lid] = new_lid
                    all_lines.append(RuntimeLine(
                        lid=new_lid,
                        pos_x=_TimeWarpEval(ln.pos_x, start_at, speed, off_i, time_offset),
                        pos_y=_TimeWarpEval(ln.pos_y, start_at, speed, off_i, time_offset),
                        rot=_TimeWarpEval(ln.rot, start_at, speed, off_i, time_offset),
                        alpha=_TimeWarpEval(ln.alpha, start_at, speed, off_i, time_offset),
                        scroll_px=_TimeWarpIntegral(ln.scroll_px, start_at, speed, off_i, time_offset),
                        color_rgb=ln.color_rgb,
                        name=ln.name,
                        event_counts=ln.event_counts
                    ))

                # map notes
                for n in notes_i:
                    if n.fake:
                        continue
                    if n.t_hit < start_local or n.t_hit > end_local:
                        continue
                    t_hit_m = start_at + (n.t_hit + off_i - time_offset) / max(1e-9, speed)
                    t_end_local = min(n.t_end, end_local)
                    t_end_m = start_at + (t_end_local + off_i - time_offset) / max(1e-9, speed)
                    new_line = lid_map.get(n.line_id, n.line_id)
                    # make hitsound absolute for advance mode
                    hs = n.hitsound_path
                    if hs:
                        hs_abs = os.path.join(base_dir, hs)
                        if os.path.exists(hs_abs):
                            hs = hs_abs
                    nn = RuntimeNote(
                        nid=n.nid,
                        line_id=new_line,
                        kind=n.kind,
                        above=n.above,
                        fake=False,
                        t_hit=t_hit_m,
                        t_end=t_end_m,
                        x_local_px=n.x_local_px,
                        y_offset_px=n.y_offset_px,
                        speed_mul=n.speed_mul,
                        size_px=n.size_px,
                        alpha01=n.alpha01,
                        hitsound_path=hs,
                        t_enter=n.t_enter,
                        mh=n.mh
                    )
                    all_notes.append(nn)

                # segment bgm schedule
                seg_dur = max(0.0, (end_local - start_local) / max(1e-9, speed))
                advance_segment_starts.append(start_at)
                advance_segment_bgm.append(seg_bgm)
                cur_start = max(cur_start, start_at + seg_dur)

            fmt = "advance"
            offset = 0.0
            lines = all_lines
            notes = sorted(all_notes, key=lambda x: x.t_hit)
            advance_main_bgm = advance_segment_bgm[0] if advance_segment_bgm else None

        else:
            # composite
            tracks = list(advance_cfg.get("tracks", []) or [])
            main_idx = int(advance_cfg.get("main", 0) or 0)
            for idx, tr in enumerate(tracks):
                inp = str(tr.get("input"))
                start_at = float(tr.get("start_at", 0.0))
                end_at = tr.get("end_at", None)
                end_at = float(end_at) if end_at is not None else None
                speed = float(tr.get("chart_speed", 1.0))
                fmt_i, off_i, lines_i, notes_i, music_p, bg_p, info, chart_p, base_dir = _load_one_input(inp)

                time_offset = float(tr.get("time_offset", 0.0)) + off_i
                lid_map: Dict[int, int] = {}
                for ln in lines_i:
                    new_lid = lid_base
                    lid_base += 1
                    lid_map[ln.lid] = new_lid
                    all_lines.append(RuntimeLine(
                        lid=new_lid,
                        pos_x=_TimeWarpEval(ln.pos_x, start_at, speed, off_i, time_offset),
                        pos_y=_TimeWarpEval(ln.pos_y, start_at, speed, off_i, time_offset),
                        rot=_TimeWarpEval(ln.rot, start_at, speed, off_i, time_offset),
                        alpha=_TimeWarpEval(ln.alpha, start_at, speed, off_i, time_offset),
                        scroll_px=_TimeWarpIntegral(ln.scroll_px, start_at, speed, off_i, time_offset),
                        color_rgb=ln.color_rgb,
                        name=ln.name,
                        event_counts=ln.event_counts
                    ))

                for n in notes_i:
                    if n.fake:
                        continue
                    t_hit_m = start_at + (n.t_hit + off_i - time_offset) / max(1e-9, speed)
                    t_end_m = start_at + (n.t_end + off_i - time_offset) / max(1e-9, speed)
                    if end_at is not None and t_hit_m > end_at:
                        continue
                    if end_at is not None:
                        t_end_m = min(t_end_m, end_at)
                    new_line = lid_map.get(n.line_id, n.line_id)
                    hs = n.hitsound_path
                    if hs:
                        hs_abs = os.path.join(base_dir, hs)
                        if os.path.exists(hs_abs):
                            hs = hs_abs
                    nn = RuntimeNote(
                        nid=n.nid,
                        line_id=new_line,
                        kind=n.kind,
                        above=n.above,
                        fake=False,
                        t_hit=t_hit_m,
                        t_end=t_end_m,
                        x_local_px=n.x_local_px,
                        y_offset_px=n.y_offset_px,
                        speed_mul=n.speed_mul,
                        size_px=n.size_px,
                        alpha01=n.alpha01,
                        hitsound_path=hs,
                        t_enter=n.t_enter,
                        mh=n.mh
                    )
                    all_notes.append(nn)

                # bgm track plan
                bgm_p = str(tr.get("bgm")) if tr.get("bgm") else (music_p if (music_p and os.path.exists(music_p)) else None)
                if bgm_p and (not os.path.isabs(bgm_p)):
                    # prefer chart base_dir, else advance.json dir
                    cand = os.path.join(base_dir, bgm_p)
                    bgm_p = cand if os.path.exists(cand) else os.path.join(advance_base_dir, bgm_p)
                if bgm_p:
                    advance_tracks_bgm.append({"start_at": start_at, "end_at": end_at, "path": bgm_p, "idx": idx})
                if idx == main_idx:
                    advance_main_bgm = bgm_p
                if (not bg_path) and bg_p and os.path.exists(bg_p):
                    bg_path = bg_p
                    chart_info = info or {}
                    bd = chart_info.get("backgroundDim", None)
                    if bd is not None:
                        bg_dim_alpha = int(clamp(float(bd), 0.0, 1.0) * 255)

            fmt = "advance"
            offset = 0.0
            lines = all_lines
            notes = sorted(all_notes, key=lambda x: x.t_hit)

        # recompute scroll samples in master time
        line_map = {ln.lid: ln for ln in lines}
        for n in notes:
            ln = line_map.get(n.line_id)
            if ln is None:
                continue
            n.scroll_hit = ln.scroll_px.integral(n.t_hit)
            n.scroll_end = ln.scroll_px.integral(n.t_end)

        # in advance mode we will manage bgm ourselves
        chart_path = None

    else:
        if os.path.isdir(args.input) or (os.path.isfile(args.input) and args.input.lower().endswith((".zip", ".pez"))):
            pack = load_chart_pack(args.input)
            chart_path = pack.chart_path
            music_path = pack.music_path
            bg_path = pack.bg_path
            chart_info = pack.info

            # info.yml: backgroundDim is 0..1 darkness opacity (overlay rectangle opacity)
            bd = chart_info.get("backgroundDim", None)
            if bd is not None:
                bg_dim_alpha = int(clamp(float(bd), 0.0, 1.0) * 255)

        # Auto bg/bgm from pack (parameter takes priority)
        auto_bg = bg_path if bg_path and os.path.exists(bg_path) else None
        auto_bgm = music_path if music_path and os.path.exists(music_path) else None

        bg_file = args.bg if args.bg else auto_bg
        bgm_file = args.bgm if args.bgm else auto_bgm

    # unify bg_file/bgm_file defaults for advance mode
    if advance_active:
        if args.bg:
            bg_file = args.bg
        else:
            bg_file = bg_path if (bg_path and os.path.exists(bg_path)) else None
        if bg_file and (not os.path.isabs(str(bg_file))):
            cand = os.path.join(advance_base_dir, str(bg_file))
            if os.path.exists(cand):
                bg_file = cand

        if args.bgm:
            bgm_file = args.bgm
        else:
            bgm_file = advance_main_bgm
        if bgm_file and (not os.path.isabs(str(bgm_file))):
            cand = os.path.join(advance_base_dir, str(bgm_file))
            if os.path.exists(cand):
                bgm_file = cand

    # Load background
    bg_base = None
    bg_blurred = None
    if bg_file:
        bg_base = pygame.image.load(bg_file).convert()
        bg_base = pygame.transform.smoothscale(bg_base, (W, H))

        def blur_surface(surf: pygame.Surface, factor: int) -> pygame.Surface:
            factor = max(1, factor)
            w, h = surf.get_size()
            small_surf = pygame.transform.smoothscale(surf, (max(1, w // factor), max(1, h // factor)))
            return pygame.transform.smoothscale(small_surf, (w, h))

        bg_blurred = blur_surface(bg_base, args.bg_blur)

    # BGM
    use_bgm_clock = False
    advance_mix_failed = False
    advance_sound_tracks: List[Dict[str, Any]] = []
    if not advance_active:
        if bgm_file:
            pygame.mixer.music.load(bgm_file)
            pygame.mixer.music.set_volume(clamp(args.bgm_volume, 0.0, 1.0))
            pygame.mixer.music.play()
            use_bgm_clock = True

        fmt, offset, lines, notes = load_chart(chart_path, W, H)
    else:
        # advance uses perf clock; BGM scheduled manually
        use_bgm_clock = False
        if advance_mix:
            # try load bgm as Sound and schedule playback
            try:
                if advance_tracks_bgm:
                    for tr in advance_tracks_bgm:
                        pth = str(tr.get("path"))
                        if pth and os.path.exists(pth):
                            snd = pygame.mixer.Sound(pth)
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
                            snd = pygame.mixer.Sound(str(pth))
                            advance_sound_tracks.append({
                                "start_at": float(advance_segment_starts[i]),
                                "end_at": None,
                                "sound": snd,
                                "channel": None,
                                "started": False,
                                "stopped": False,
                            })
                # fill end_at for sequence tracks so previous stops when next starts
                if advance_sound_tracks and advance_segment_starts:
                    adv_sorted = sorted(range(len(advance_sound_tracks)), key=lambda k: float(advance_sound_tracks[k]["start_at"]))
                    for j in range(len(adv_sorted) - 1):
                        cur = advance_sound_tracks[adv_sorted[j]]
                        nxt = advance_sound_tracks[adv_sorted[j+1]]
                        cur["end_at"] = float(nxt["start_at"])
            except:
                advance_mix_failed = True
                advance_sound_tracks = []
        if (not advance_mix) or advance_mix_failed:
            # use mixer.music switching
            try:
                if advance_segment_bgm:
                    # sequence mode: start first segment BGM
                    if advance_segment_bgm[0] and os.path.exists(str(advance_segment_bgm[0])):
                        pygame.mixer.music.load(str(advance_segment_bgm[0]))
                        pygame.mixer.music.set_volume(clamp(args.bgm_volume, 0.0, 1.0))
                        pygame.mixer.music.play()
                        advance_bgm_active = True
                        advance_segment_idx = 0
                elif bgm_file and os.path.exists(str(bgm_file)):
                    pygame.mixer.music.load(str(bgm_file))
                    pygame.mixer.music.set_volume(clamp(args.bgm_volume, 0.0, 1.0))
                    pygame.mixer.music.play()
                    advance_bgm_active = True
            except:
                pass
    if not args.multicolor_lines:
        for ln in lines:
            ln.color_rgb = (255, 255, 255)

    # Get chart directory for RPE hitsound relative paths
    chart_dir = os.path.dirname(os.path.abspath(chart_path)) if chart_path else (advance_base_dir if advance_active else os.getcwd())

    # Precompute first entry time for each note (for culling without time window)
    precompute_t_enter(lines, notes, W, H)

    # Apply start_time/end_time filtering for single charts (not advance mode)
    if not advance_active and (args.start_time is not None or args.end_time is not None):
        start_time = args.start_time if args.start_time is not None else -float('inf')
        end_time = args.end_time if args.end_time is not None else float('inf')
        
        # Filter notes to only those within the time range
        filtered_notes = []
        for n in notes:
            if n.fake:
                continue
            # Check if note is within range (use t_hit for non-hold, consider hold duration for hold notes)
            if n.kind == 3:
                # Hold note: include if any part overlaps
                if n.t_end < start_time or n.t_hit > end_time:
                    continue
            else:
                # Normal note: check hit time
                if n.t_hit < start_time or n.t_hit > end_time:
                    continue
            filtered_notes.append(n)
        notes = filtered_notes

    # Minimal simultaneous grouping: if >=2 notes within eps at same t_hit => mh
    eps = 1e-4
    i = 0
    while i < len(notes):
        j = i + 1
        while j < len(notes) and abs(notes[j].t_hit - notes[i].t_hit) <= eps:
            j += 1
        if (j - i) >= 2:
            for k in range(i, j):
                notes[k].mh = True
        i = j

    # Total notes (excluding fake notes) - count all unique notes in composite mode
    if advance_active and advance_cfg and advance_cfg.get("mode") == "composite":
        # For composite mode, count all unique notes across all tracks
        unique_notes = set()
        tracks = advance_cfg.get("tracks", [])
        for track in tracks:
            inp = str(track.get("input"))
            if os.path.isdir(inp) or (os.path.isfile(inp) and str(inp).lower().endswith((".zip", ".pez"))):
                p = load_chart_pack(inp)
                chart_p = p.chart_path
            else:
                chart_p = inp
            fmt_i, off_i, lines_i, notes_i = load_chart(chart_p, W, H)
            for n in notes_i:
                if not n.fake:
                    unique_notes.add((n.nid, n.t_hit, n.line_id))
        total_notes = len(unique_notes)
    else:
        # For single chart or sequence mode, count notes after filtering
        total_notes = sum(1 for n in notes if not n.fake)
    
    # Calculate chart end considering time range
    if not advance_active and args.end_time is not None:
        chart_end = min(args.end_time, max((n.t_end for n in notes if not n.fake), default=args.end_time))
    else:
        chart_end = max((n.t_end for n in notes if not n.fake), default=0.0)

    # Per-line sorted hit times for stats
    note_times_by_line: Dict[int, List[float]] = {}
    for n in notes:
        if n.fake:
            continue
        note_times_by_line.setdefault(n.line_id, []).append(n.t_hit)
    for k in note_times_by_line:
        note_times_by_line[k].sort()

    def _line_note_counts(lid: int, t: float) -> Tuple[int, int]:
        arr = note_times_by_line.get(lid, [])
        past = bisect.bisect_left(arr, t)
        incoming = bisect.bisect_right(arr, t + float(args.approach)) - past
        return past, incoming

    def _track_seg_state(tr: Any) -> str:
        if hasattr(tr, "segs") and isinstance(getattr(tr, "segs"), list):
            total = len(tr.segs)
            idx = getattr(tr, "i", 0)
            return f"{idx}/{max(0,total-1)}"
        return "-"

    def _scroll_speed_px_per_sec(ln: RuntimeLine, t: float) -> float:
        dt = 0.01
        a = ln.scroll_px.integral(t - dt)
        b = ln.scroll_px.integral(t + dt)
        return (b - a) / (2 * dt)

    # Hitsound playback function
    def play_hitsound(note: RuntimeNote):
        now_tick = pygame.time.get_ticks()
        # RPE custom hitsound has priority
        if note.hitsound_path:
            fp = str(note.hitsound_path)
            if (not os.path.isabs(fp)):
                fp = os.path.join(chart_dir, fp)
            if os.path.exists(fp):
                if hitsound_min_interval_ms > 0:
                    last = last_hitsound_ms.get(fp, -10**9)
                    if now_tick - last < hitsound_min_interval_ms:
                        return
                try:
                    snd = custom_sfx_cache.get(fp)
                    if snd is None:
                        snd = pygame.mixer.Sound(fp)
                        custom_sfx_cache[fp] = snd
                    snd.play()
                    last_hitsound_ms[fp] = now_tick
                    return
                except:
                    pass

        if not respack:
            return

        # respack: click/drag/flick.ogg
        if note.kind == 1: key = "click"
        elif note.kind == 2: key = "drag"
        elif note.kind == 4: key = "flick"
        elif note.kind == 3: key = "click"  # hold head uses click
        else: key = "click"

        if hitsound_min_interval_ms > 0:
            last = last_hitsound_ms.get(key, -10**9)
            if now_tick - last < hitsound_min_interval_ms:
                return

        s = respack.sfx.get(key)
        if s:
            s.play()
            last_hitsound_ms[key] = now_tick

    # Pick note sprite image
    def pick_note_image(note: RuntimeNote) -> Optional[pygame.Surface]:
        if not respack:
            return None
        # Select proper image based on note type and mh status
        if note.kind == 1:  # tap
            return respack.img["click_mh.png"] if note.mh else respack.img["click.png"]
        elif note.kind == 2:  # drag
            return respack.img["drag_mh.png"] if note.mh else respack.img["drag.png"]
        elif note.kind == 3:  # hold (handled separately in draw_hold_3slice)
            return respack.img["hold_mh.png"] if note.mh else respack.img["hold.png"]
        elif note.kind == 4:  # flick
            return respack.img["flick_mh.png"] if note.mh else respack.img["flick.png"]
        else:
            return respack.img["click_mh.png"] if note.mh else respack.img["click.png"]

    # Draw hitfx from sprite sheet
    def draw_hitfx(overlay: pygame.Surface, fx: HitFX, t: float):
        if not respack:
            # fallback ring
            age = t - fx.t0
            if age < 0 or age > 0.18:
                return
            r = int(10 + 140 * age)
            a = int(255 * (1.0 - age / 0.18))
            rr, gg, bb, _ = fx.rgba
            x0, y0 = apply_expand_xy(fx.x, fx.y, W, H, expand)
            draw_ring(overlay, x0, y0, max(1, int(r / expand)), (rr, gg, bb, a), thickness=3)
            return

        age = t - fx.t0
        dur = max(1e-6, respack.hitfx_duration)
        if age < 0 or age > dur:
            return

        fw, fh = respack.hitfx_frames_xy
        sheet = respack.hitfx_sheet
        sw, sh = sheet.get_width(), sheet.get_height()
        cell_w, cell_h = sw // fw, sh // fh

        p = clamp(age / dur, 0.0, 0.999999)
        idx = int(p * (fw * fh))
        ix = idx % fw
        iy = idx // fw

        frame = sheet.subsurface((ix * cell_w, iy * cell_h, cell_w, cell_h))

        # scale
        sc = (respack.hitfx_scale * float(args.hitfx_scale_mul)) / float(expand)
        if sc != 1.0:
            frame = pygame.transform.smoothscale(frame, (int(cell_w * sc), int(cell_h * sc)))

        # rotate with note/line if enabled
        if respack.hitfx_rotate:
            frame = pygame.transform.rotozoom(frame, -fx.rot * 180.0 / math.pi, 1.0)

        if respack.hitfx_tinted:
            r, g, b, a = fx.rgba
            tint_s = pygame.Surface(frame.get_size(), pygame.SRCALPHA)
            tint_s.fill((r, g, b, 255))
            frame = frame.copy()
            frame.blit(tint_s, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
            frame.set_alpha(a)

        x0, y0 = apply_expand_xy(fx.x, fx.y, W, H, expand)
        overlay.blit(frame, (x0 - frame.get_width()/2, y0 - frame.get_height()/2))

    # Note states
    states = [NoteState(n) for n in notes]
    # keep a cursor for global time ordering
    idx_next = 0

    judge = Judge()
    hitfx: List[HitFX] = []
    particles: List[ParticleBurst] = []

    hold_fx_interval_ms = max(10, int(args.hold_fx_interval_ms))
    hold_tail_tol = clamp(float(args.hold_tail_tol), 0.0, 1.0)
    hitsound_min_interval_ms = max(0, int(args.hitsound_min_interval_ms))

    last_hitsound_ms: Dict[str, int] = {}
    custom_sfx_cache: Dict[str, pygame.mixer.Sound] = {}

    # timebase (no music)
    t0 = now_sec()
    paused = False
    pause_t = 0.0
    pause_frame: Optional[pygame.Surface] = None

    # input
    holding_input = False
    key_down = False
    def input_down():
        return holding_input or key_down

    # previous for edge
    prev_down = False

    # sizes
    base_note_w = int(0.06 * W)
    base_note_h = int(0.018 * H)
    note_scale_x = float(args.note_scale_x)
    note_scale_y = float(args.note_scale_y)
    hold_body_w = int(0.035 * W * note_scale_x)

    outline_w = max(1, int(round(2.0 / float(expand))))
    line_w = max(1, int(round(4.0 / float(expand))))
    dot_r = max(1, int(round(4.0 / float(expand))))

    # judge line length: 6.75 * base_note_w (render width)
    line_len = int(6.75 * W)

    running = True
    while running:
        dt_frame = clock.tick(120) / 1000.0

        for ev in pygame.event.get():
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
                        # freeze current screen frame
                        try:
                            pause_frame = screen.copy()
                        except:
                            pause_frame = None
                        if use_bgm_clock:
                            pygame.mixer.music.pause()
                    else:
                        if use_bgm_clock:
                            pygame.mixer.music.unpause()
                        else:
                            t0 += now_sec() - pause_t
                        pause_frame = None
                elif ev.key == pygame.K_r:
                    # restart
                    t0 = now_sec()
                    paused = False
                    if use_bgm_clock:
                        pygame.mixer.music.stop()
                        pygame.mixer.music.play()
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
            elif ev.type == pygame.MOUSEBUTTONUP and ev.button == 1:
                holding_input = False

        if paused:
            if pause_frame is not None:
                screen.blit(pause_frame, (0, 0))
            else:
                screen.fill((10, 10, 15))
            txt = font.render("PAUSED (P to resume)", True, (220, 220, 220))
            screen.blit(txt, (W//2 - txt.get_width()//2, H//2))
            pygame.display.flip()
            continue

        # chart time (offset convention): chartTime = audioTime - offset
        # Apply start_time offset for single charts when specified
        if use_bgm_clock:
            # ms since music started; -1 means not available yet, clamp to 0
            ms = pygame.mixer.music.get_pos()
            audio_t = max(0.0, ms / 1000.0)
            t = (audio_t - offset) * float(args.chart_speed)
        else:
            t = ((now_sec() - t0) - offset) * float(args.chart_speed)
        
        # Apply start_time offset for single charts (not advance mode)
        if not advance_active and args.start_time is not None:
            t += args.start_time

        # Draw background (blurred + dimmed)
        if bg_blurred:
            screen.blit(bg_blurred, (0, 0))
        else:
            screen.fill((10, 10, 14))

        # dim overlay (use info.yml backgroundDim if available)
        dim = bg_dim_alpha if (bg_dim_alpha is not None) else clamp(args.bg_dim, 0, 255)
        if dim > 0:
            dim_surf = pygame.Surface((W, H), pygame.SRCALPHA)
            dim_surf.fill((0, 0, 0, dim))
            screen.blit(dim_surf, (0, 0))

        # overlay for RGBA drawing
        overlay = pygame.Surface((W, H), pygame.SRCALPHA)

        # debug draw calls to render after notes (so they won't be covered)
        line_text_draw_calls: List[Tuple[int, pygame.Surface, float, float]] = []

        # track which judge line was hit recently for layering
        if 'line_last_hit_ms' not in locals():
            line_last_hit_ms: Dict[int, int] = {}

        def _mark_line_hit(lid: int):
            line_last_hit_ms[lid] = pygame.time.get_ticks()

        # --------------------------------
        # Autoplay
        # --------------------------------
        if args.autoplay:
            # iterate a small window near idx_next
            for s in states[max(0, idx_next-20): min(len(states), idx_next+300)]:
                if s.judged or s.note.fake:
                    continue
                n = s.note
                if n.kind != 3:
                    if abs(t - n.t_hit) <= Judge.PERFECT:
                        judge.bump()
                        s.judged = True
                        s.hit = True
                        # fx at note position
                        ln = lines[n.line_id]
                        lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if respack:
                            c = respack.judge_colors.get("PERFECT", c)
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, pygame.time.get_ticks(), int(respack.hitfx_duration * 1000), c))
                        _mark_line_hit(n.line_id)
                        play_hitsound(n)
                else:
                    if not s.holding and abs(t - n.t_hit) <= Judge.PERFECT:
                        s.hit = True
                        s.holding = True
                        s.next_hold_fx_ms = pygame.time.get_ticks() + hold_fx_interval_ms
                        s.hold_grade = "PERFECT"
                        ln = lines[n.line_id]
                        lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if respack:
                            c = respack.judge_colors.get("PERFECT", c)
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, pygame.time.get_ticks(), int(respack.hitfx_duration * 1000), c))
                        play_hitsound(n)
                    if s.holding and t >= n.t_end:
                        s.holding = False

        # --------------------------------
        # Manual hit (global nearest-by-time)
        # --------------------------------
        down = input_down()
        press_edge = down and not prev_down
        prev_down = down

        if press_edge and not args.autoplay:
            best = None
            best_dt = 1e9
            # search around cursor for speed
            for s in states[max(0, idx_next-50): min(len(states), idx_next+500)]:
                if s.judged or s.note.fake:
                    continue
                dt = abs(t - s.note.t_hit)
                if dt <= Judge.BAD and dt < best_dt:
                    best = s
                    best_dt = dt

            if best is not None:
                n = best.note
                if n.kind == 3:
                    grade = judge.grade_window(n.t_hit, t)
                    if grade is not None:
                        best.hit = True
                        best.holding = True
                        best.hold_grade = grade
                        best.next_hold_fx_ms = pygame.time.get_ticks() + hold_fx_interval_ms
                        ln = lines[n.line_id]
                        lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if respack:
                            c = (respack.judge_colors.get(grade)
                                 or respack.judge_colors.get("GOOD")
                                 or respack.judge_colors.get("PERFECT")
                                 or c)
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, pygame.time.get_ticks(), int(respack.hitfx_duration * 1000), c))
                        play_hitsound(n)
                else:
                    grade = judge.try_hit(best, t)
                    if grade is not None:
                        ln = lines[n.line_id]
                        lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
                        c = (255, 255, 255, 255)
                        if respack:
                            c = (respack.judge_colors.get(grade)
                                 or respack.judge_colors.get("GOOD")
                                 or respack.judge_colors.get("PERFECT")
                                 or c)
                        hitfx.append(HitFX(x, y, t, c, lr))
                        if respack and (not respack.hide_particles):
                            particles.append(ParticleBurst(x, y, pygame.time.get_ticks(), int(respack.hitfx_duration * 1000), c))
                        play_hitsound(n)

        # hold maintenance
        if not args.autoplay:
            for s in states[max(0, idx_next-50): min(len(states), idx_next+500)]:
                if s.judged or s.note.fake:
                    continue
                n = s.note
                if n.kind == 3 and s.holding:
                    if not input_down() and t < n.t_end - 1e-6:
                        s.released_early = True
                        s.holding = False
                    if t >= n.t_end:
                        s.holding = False

        # hold finalize (tail tolerance): decide success/fail but keep rendering until t_end
        for s in states[max(0, idx_next-200): min(len(states), idx_next+800)]:
            n = s.note
            if n.fake or n.kind != 3 or s.hold_finalized:
                continue

            # if never hit and miss window passed: mark failed (darken), but don't hide
            if (not s.hit) and (not s.hold_failed) and (t > n.t_hit + Judge.BAD):
                s.hold_failed = True
                judge.break_combo()

            # early release: decide by tolerance
            if s.released_early and (not s.hold_finalized):
                dur = max(1e-6, (n.t_end - n.t_hit))
                prog = clamp((t - n.t_hit) / dur, 0.0, 1.0)
                if prog < hold_tail_tol:
                    s.hold_failed = True
                    judge.break_combo()
                else:
                    # success early (still render until end)
                    g = s.hold_grade or "PERFECT"
                    judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    judge.judged_cnt += 1
                    judge.bump()
                    s.hold_finalized = True

            # end time: finalize if not done yet
            if t >= n.t_end and (not s.hold_finalized):
                if s.hit and (not s.hold_failed):
                    g = s.hold_grade or "PERFECT"
                    judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    judge.judged_cnt += 1
                    judge.bump()
                else:
                    judge.mark_miss(s)
                s.hold_finalized = True
                s.judged = True

        # hold tick fx: spawn every hold_fx_interval_ms while holding
        if respack:
            now_tick = pygame.time.get_ticks()
            for s in states[max(0, idx_next-200): min(len(states), idx_next+800)]:
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
                    lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
                    # spawn at head position locked on the judge line (dy = 0)
                    x, y = note_world_pos(lx, ly, lr, sc, n, sc, for_tail=False)
                    c = respack.judge_colors.get("PERFECT", (255, 255, 255, 255))
                    hitfx.append(HitFX(x, y, t, c, lr))
                    if not respack.hide_particles:
                        particles.append(ParticleBurst(x, y, pygame.time.get_ticks(), int(respack.hitfx_duration * 1000), c))
                    _mark_line_hit(n.line_id)
                    s.next_hold_fx_ms += hold_fx_interval_ms

        # miss detection (exclude hold; holds are finalized by tail tolerance)
        miss_window = Judge.BAD
        for s in states[max(0, idx_next-200): min(len(states), idx_next+800)]:
            if s.judged or s.note.fake:
                continue
            if s.note.kind == 3:
                continue
            if t > s.note.t_hit + miss_window:
                judge.mark_miss(s)

        # advance cursor
        while idx_next < len(states) and states[idx_next].judged:
            idx_next += 1

        # --------------------------------
        # Draw all judge lines
        # --------------------------------
        for ln in lines:
            lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
            # line endpoints
            tx, ty = math.cos(lr), math.sin(lr)
            ex = tx * (line_len * 0.5)
            ey = ty * (line_len * 0.5)
            p0 = (lx - ex, ly - ey)
            p1 = (lx + ex, ly + ey)
            p0s = apply_expand_xy(p0[0], p0[1], W, H, expand)
            p1s = apply_expand_xy(p1[0], p1[1], W, H, expand)
            rgba = (*ln.color_rgb, int(255 * la))
            draw_line_rgba(overlay, p0s, p1s, rgba, width=line_w)
            # center dot
            lxs, lys = apply_expand_xy(lx, ly, W, H, expand)
            pygame.draw.circle(overlay, (*ln.color_rgb, int(220 * la)), (int(lxs), int(lys)), dot_r)

            if args.debug_judge_windows:
                spd = abs(_scroll_speed_px_per_sec(ln, t))
                d_bad = max(2.0, spd * Judge.BAD)
                d_good = max(2.0, spd * Judge.GOOD)
                d_perf = max(2.0, spd * Judge.PERFECT)
                for hh, col in [
                    (2 * d_bad, (255, 80, 80, 36)),
                    (2 * d_good, (255, 220, 80, 40)),
                    (2 * d_perf, (80, 255, 160, 44)),
                ]:
                    ptsw = rect_corners(lx, ly, float(line_len), float(hh), lr)
                    draw_poly_rgba(overlay, apply_expand_pts(ptsw, W, H, expand), col)

            # defer text drawing until after notes/hitfx to avoid being covered
            pr = int(line_last_hit_ms.get(ln.lid, 0))
            if args.debug_line_label:
                label = ln.name.strip() if ln.name.strip() else str(ln.lid)
                txt = small.render(label, True, (240, 240, 240))
                lxs, lys = apply_expand_xy(lx, ly, W, H, expand)
                line_text_draw_calls.append((pr, txt, lxs - txt.get_width()/2, lys - txt.get_height()/2))

            if args.debug_line_stats:
                past, incoming = _line_note_counts(ln.lid, t)
                ec = ln.event_counts or {}
                seg_pos = _track_seg_state(ln.pos_x)
                seg_rot = _track_seg_state(ln.rot)
                seg_a = _track_seg_state(ln.alpha)
                s1 = f"L{ln.lid} N {past}/{past+incoming}  in {incoming}"
                s2 = f"E mv {ec.get('move', ec.get('moveX', 0)+ec.get('moveY', 0))} r {ec.get('rot', 0)} a {ec.get('alpha', 0)} sp {ec.get('speed', 0)}"
                s3 = f"seg p {seg_pos} r {seg_rot} a {seg_a}"
                t1 = small.render(s1, True, (220, 220, 220))
                t2 = small.render(s2, True, (180, 180, 180))
                t3 = small.render(s3, True, (160, 160, 160))
                dy = -18 if ln.lid % 2 == 0 else 18
                lxs, lys = apply_expand_xy(lx, ly, W, H, expand)
                line_text_draw_calls.append((pr, t1, lxs - t1.get_width()/2, lys + dy - 40))
                line_text_draw_calls.append((pr, t2, lxs - t2.get_width()/2, lys + dy - 22))
                line_text_draw_calls.append((pr, t3, lxs - t3.get_width()/2, lys + dy - 4))

        # --------------------------------
        # Draw notes (use t_enter for culling instead of time window)
        # --------------------------------
        # find start index for drawing (simple linear from idx_next backward)
        start_i = 0
        if not args.no_cull:
            start_i = max(0, idx_next - 800)

        for s in states[start_i:]:
            n = s.note
            if n.fake:
                continue
            if n.kind != 3 and s.judged:
                continue
            if n.kind == 3 and t > n.t_end:
                continue

            # Use precomputed t_enter instead of time window
            if not args.no_cull and t < n.t_enter:
                continue

            ln = lines[n.line_id]
            lx, ly, lr, la, sc_now, la_raw = eval_line_state(ln, t)

            # per-note alpha and color
            alpha_mul = 1.0
            if args.line_alpha_affects_notes == "always":
                alpha_mul = clamp(abs(la_raw), 0.0, 1.0)
            elif args.line_alpha_affects_notes == "negative_only" and la_raw < 0.0:
                alpha_mul = clamp(abs(la_raw), 0.0, 1.0)
            note_alpha = clamp(alpha_mul * n.alpha01, 0.0, 1.0)
            type_rgb = NOTE_TYPE_COLORS.get(n.kind, (255, 255, 255))
            rgba_fill = (*type_rgb, int(255 * note_alpha))
            rgba_outline = (*ln.color_rgb, int(255 * note_alpha))

            # sizes
            w = base_note_w * n.size_px * note_scale_x
            h = base_note_h * n.size_px * note_scale_y
            ws = w / float(expand)
            hs = h / float(expand)

            if n.kind == 3:
                # hold: use 3-slice atlas rendering
                if s.hit:
                    head = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
                else:
                    # before being hit, hold head should never pass through the judge line
                    # if time exceeds t_hit, clamp head to stay on the line (dy>=0)
                    head_target_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
                    head = note_world_pos(lx, ly, lr, sc_now, n, head_target_scroll, for_tail=False)
                tail = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_end, for_tail=True)

                head_s = apply_expand_xy(head[0], head[1], W, H, expand)
                tail_s = apply_expand_xy(tail[0], tail[1], W, H, expand)

                hold_alpha = note_alpha
                if s.hold_failed:
                    hold_alpha *= 0.35

                # Color and alpha already calculated in outer scope: note_alpha / ln.color_rgb / rgb
                draw_hold_3slice(
                    overlay=overlay,
                    head_xy=head_s,
                    tail_xy=tail_s,
                    line_rot=lr,
                    alpha01=hold_alpha,
                    line_rgb=ln.color_rgb,
                    note_rgb=type_rgb,
                    size_scale=n.size_px,
                    mh=n.mh,
                    hold_body_w=int(hold_body_w / float(expand)),
                    draw_outline=(not args.no_note_outline),
                    outline_width=outline_w
                )

                if args.debug_note_info:
                    dur = max(1e-6, (n.t_end - n.t_hit))
                    prog = clamp((t - n.t_hit) / dur, 0.0, 1.0)
                    info = f"HOLD L{n.line_id} x={n.x_local_px:.1f} t={n.t_hit:.3f}->{n.t_end:.3f} p={prog:.2f} hit={int(s.hit)} hold={int(s.holding)} fail={int(s.hold_failed)}"
                    txt = small.render(info, True, (230, 230, 230))
                    overlay.blit(txt, (head_s[0] - txt.get_width()/2, head_s[1] + 14))

            else:
                # normal note: try sprite first, fallback to rect
                p = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                ps = apply_expand_xy(p[0], p[1], W, H, expand)
                img = pick_note_image(n)
                if img is None:
                    # fallback polygon
                    pts = rect_corners(ps[0], ps[1], ws, hs, lr)
                    draw_poly_rgba(overlay, pts, rgba_fill)
                    if not args.no_note_outline:
                        draw_poly_outline_rgba(overlay, pts, rgba_outline, width=outline_w)

                else:
                    # Scale sprite to match target note size
                    iw, ih = img.get_width(), img.get_height()
                    target_w = max(1, int(ws))
                    target_h = max(1, int(target_w * ih / max(1, iw) * note_scale_y))
                    scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                    # Rotate
                    rotated = pygame.transform.rotate(scaled, -lr * 180.0 / math.pi)

                    # alpha
                    rotated = rotated.copy()
                    rotated.set_alpha(int(255 * note_alpha))

                    overlay.blit(rotated, (ps[0] - rotated.get_width()/2, ps[1] - rotated.get_height()/2))
                    pts = rect_corners(ps[0], ps[1], float(target_w), float(target_h), lr)
                    if not args.no_note_outline:
                        draw_poly_outline_rgba(overlay, pts, rgba_outline, width=outline_w)

                if args.debug_note_info:
                    info = f"N{n.nid} L{n.line_id} k={n.kind} x={n.x_local_px:.1f} t={n.t_hit:.3f} dt={t-n.t_hit:+.3f}"
                    txt = small.render(info, True, (230, 230, 230))
                    overlay.blit(txt, (ps[0] - txt.get_width()/2, ps[1] + 14))

        # --------------------------------
        # Hit effects
        # --------------------------------
        hitfx[:] = [fx for fx in hitfx if (t - fx.t0) <= (respack.hitfx_duration if respack else 0.18)]
        for fx in hitfx:
            draw_hitfx(overlay, fx, t)

        # composite overlay
        screen.blit(overlay, (0, 0))

        # base viewport rectangle when expanded
        if expand > 1.0:
            bw = W / expand
            bh = H / expand
            x0 = (W - bw) * 0.5
            y0 = (H - bh) * 0.5
            pygame.draw.rect(screen, (240, 240, 240), pygame.Rect(int(x0), int(y0), int(bw), int(bh)), 2)

        # particles: draw directly on screen with additive blend so they remain visible on all backgrounds
        now_ms = pygame.time.get_ticks()
        particles[:] = [p for p in particles if p.alive(now_ms)]
        for p in particles:
            # draw particles in expanded view using transformed coordinates
            parts = p.get_particles(now_ms)
            if hasattr(pygame, "BLEND_RGBA_ADD"):
                blend_flag = pygame.BLEND_RGBA_ADD
            elif hasattr(pygame, "BLEND_ADD"):
                blend_flag = pygame.BLEND_ADD
            else:
                blend_flag = 0
            for q in parts:
                xq, yq = apply_expand_xy(q['x'], q['y'], W, H, expand)
                sz = max(1, int(q['size'] / float(expand)))
                sq = pygame.Surface((sz, sz), pygame.SRCALPHA)
                sq.fill(q['color'])
                screen.blit(sq, (int(xq - sz/2), int(yq - sz/2)), special_flags=blend_flag)

        # deferred line debug text on top (recently-hit lines draw last)
        if line_text_draw_calls:
            line_text_draw_calls.sort(key=lambda x: x[0])
            for _pr, surf, x0, y0 in line_text_draw_calls:
                screen.blit(surf, (x0, y0))

        if args.debug_particles:
            txt = small.render(f"particles={len(particles)}", True, (220, 220, 220))
            screen.blit(txt, (16, 94))

        # progress bar at top
        if chart_end > 1e-6:
            # Adjust progress calculation for start_time offset
            display_t = t
            display_end = chart_end
            if not advance_active and args.start_time is not None:
                display_t = t - args.start_time
                display_end = chart_end - args.start_time
            pbar = clamp(display_t / display_end, 0.0, 1.0) if display_end > 0 else 0.0
            pygame.draw.rect(screen, (40, 40, 40), pygame.Rect(0, 0, W, 6))
            pygame.draw.rect(screen, (230, 230, 230), pygame.Rect(0, 0, int(W * pbar), 6))

        # UI
        combo_txt = font.render(f"COMBO {judge.combo}", True, (240, 240, 240))
        screen.blit(combo_txt, (16, 14))

        # Acc / Score
        acc_ratio = (judge.acc_sum / total_notes) if total_notes > 0 else 0.0
        combo_ratio = (judge.max_combo / total_notes) if total_notes > 0 else 0.0
        score = int(((acc_ratio * 0.9) + (combo_ratio * 0.1)) * 1000000)
        score_txt = small.render(f"SCORE {score:07d}   HIT {acc_ratio*100:6.2f}%   MAX {judge.max_combo}/{total_notes}", True, (200, 200, 200))
        screen.blit(score_txt, (16, 46))

        fmt_txt = small.render(f"fmt={fmt}  t={t:7.3f}s  next={idx_next}/{len(states)}  lines={len(lines)}", True, (180, 180, 180))
        screen.blit(fmt_txt, (16, 70))

        # Top-right: chart name / level / difficulty
        if chart_info and (not args.no_title_overlay):
            nm = str(chart_info.get("name", ""))
            lv = str(chart_info.get("level", ""))
            diff = chart_info.get("difficulty", None)
            diff_s = f"{float(diff):.1f}" if diff is not None else ""
            title = f"{nm}"
            sub = f"{lv}  ({diff_s})" if (lv or diff_s) else ""

            t1 = small.render(title, True, (230, 230, 230))
            t2 = small.render(sub, True, (180, 180, 180))
            screen.blit(t1, (W - 16 - t1.get_width(), 14))
            if sub:
                screen.blit(t2, (W - 16 - t2.get_width(), 14 + 22))

        hint = small.render("LMB/SPACE: hit   hold: keep pressed   P: pause   R: restart   ESC: quit", True, (160, 160, 160))
        screen.blit(hint, (16, H - 26))

        pygame.display.flip()

    pygame.quit()

