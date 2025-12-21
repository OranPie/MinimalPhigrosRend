from __future__ import annotations

import json
import argparse
import signal
import sys
import os
import logging
from typing import Any, Dict, Optional

from . import state
from .math import easing

from .runtime.mods import apply_mods
from .runtime.advance import load_from_args
from .renderer import run as run_renderer
from .config_v2 import dump_config_v2, flatten_config_v2, load_config_v2
from .i18n import normalize_lang, pick_lang_from_config, tr
from .logging_setup import setup_logging
from .api.playlist import run_playlist_script

# Global respack instance (kept for backward-compat with the original single-file code)
respack: Optional[Any] = None

def main():
    global respack  # Declare we're using the global variable

    logger = logging.getLogger(__name__)

    ap = argparse.ArgumentParser(prog="phic_renderer")
    g_in = ap.add_argument_group("Input")
    g_in.add_argument("--input", required=False, default=None, help="chart.json OR chart pack folder OR .zip/.pez pack")
    g_in.add_argument("--advance", type=str, default=None)
    g_in.add_argument("--playlist_script", type=str, default=None, help="Run a playlist script (python file)")
    g_in.add_argument("--playlist_charts_dir", type=str, default="charts")
    g_in.add_argument("--playlist_notes_per_chart", type=int, default=10)
    g_in.add_argument("--playlist_seed", type=int, default=None)
    g_in.add_argument("--playlist_no_shuffle", action="store_true")
    g_in.add_argument("--playlist_switch_mode", type=str, default="hit", choices=["hit", "judged"])
    g_in.add_argument("--playlist_filter_levels", type=str, default=None)
    g_in.add_argument("--playlist_filter_name_contains", type=str, default=None)
    g_in.add_argument("--playlist_filter_min_total_notes", type=int, default=None)
    g_in.add_argument("--playlist_filter_max_total_notes", type=int, default=None)
    g_in.add_argument("--playlist_filter_limit", type=int, default=None)
    g_in.add_argument("--playlist_start_mode", type=str, default="fresh", choices=["fresh", "resume"])
    g_in.add_argument("--playlist_start_index", type=int, default=None)
    g_in.add_argument("--playlist_start_from_hit_total", type=int, default=None)
    g_in.add_argument("--playlist_start_from_combo_total", type=int, default=None)

    g_cfg = ap.add_argument_group("Config")
    g_cfg.add_argument("--config", type=str, default=None, help="Config v2 (JSONC) path")
    g_cfg.add_argument("--config_old", type=str, default=None, help="Legacy config (plain JSON) path")
    g_cfg.add_argument("--save_config", type=str, default=None, help="Write config v2 (JSONC) to this path")

    g_cui = ap.add_argument_group("CUI")
    g_cui.add_argument("--lang", type=str, default=None, help="Language: zh-CN / en")
    g_cui.add_argument("--quiet", action="store_true", help="Less console output")
    g_cui.add_argument("--no_color", action="store_true", help="Disable ANSI colors")

    g_win = ap.add_argument_group("Window")
    g_win.add_argument("--w", type=int, default=1280)
    g_win.add_argument("--h", type=int, default=720)

    g_render = ap.add_argument_group("Render")
    g_render.add_argument("--approach", type=float, default=3.0, help="Seconds ahead to draw")
    g_render.add_argument("--chart_speed", type=float, default=1.0)
    g_render.add_argument("--no_cull", action="store_true", help="Draw all notes (can be slow)")
    g_render.add_argument("--no_cull_screen", action="store_true", help="Disable screen-space visibility culling")
    g_render.add_argument("--no_cull_enter_time", action="store_true", help="Disable t_enter / time-window culling")
    g_render.add_argument("--note_scale_x", type=float, default=1.0)
    g_render.add_argument("--note_scale_y", type=float, default=1.0)
    g_render.add_argument("--note_flow_speed_multiplier", type=float, default=1.0)
    g_render.add_argument("--expand", type=float, default=1.0)
    g_render.add_argument("--overrender", type=float, default=2.0)
    g_render.add_argument("--trail_alpha", type=float, default=0.0)
    g_render.add_argument("--trail_blur", type=int, default=0)
    g_render.add_argument("--trail_dim", type=int, default=0)
    g_render.add_argument("--hitfx_scale_mul", type=float, default=1.0)
    g_render.add_argument("--multicolor_lines", action="store_true")
    g_render.add_argument("--no_note_outline", action="store_true")
    g_render.add_argument("--line_alpha_affects_notes", type=str, default="negative_only", choices=["never", "negative_only", "always"])
    g_render.add_argument(
        "--backend",
        type=str,
        default="pygame",
        choices=["pygame", "moderngl", "gl", "opengl"],
        help="Render backend",
    )

    g_assets = ap.add_argument_group("Assets")
    g_assets.add_argument("--respack", type=str, default=None, help="Respack zip path")
    g_assets.add_argument("--bg", type=str, default=None, help="Background image path")
    g_assets.add_argument("--bg_blur", type=int, default=10, help="Blur strength (downscale factor)")
    g_assets.add_argument("--bg_dim", type=int, default=120, help="Dark overlay alpha 0..255")

    g_audio = ap.add_argument_group("Audio")
    g_audio.add_argument("--bgm", type=str, default=None, help="BGM audio file path (ogg/mp3/wav)")
    g_audio.add_argument("--force", action="store_true", help="Force using --bgm (or config audio.bgm) even if input is a chart pack that provides music")
    g_audio.add_argument("--bgm_volume", type=float, default=0.8)
    g_audio.add_argument(
        "--audio_backend",
        type=str,
        default="pygame",
        choices=["pygame", "openal", "al"],
        help="Audio backend",
    )
    g_audio.add_argument("--hitsound_min_interval_ms", type=int, default=30)

    g_game = ap.add_argument_group("Gameplay")
    g_game.add_argument("--autoplay", action="store_true")
    g_game.add_argument("--judge_script", type=str, default=None, help="Optional judge script JSON to simulate non-perfect autoplay")
    g_game.add_argument("--hold_fx_interval_ms", type=int, default=200)
    g_game.add_argument("--hold_tail_tol", type=float, default=0.8)
    g_game.add_argument("--judge_width", type=float, default=0.12, help="Judgement width as ratio of screen width")
    g_game.add_argument("--flick_threshold", type=float, default=0.02, help="Flick gesture threshold as ratio of screen width")
    g_game.add_argument("--start_time", type=float, default=None, help="Start time in seconds (cuts chart before this time)")
    g_game.add_argument("--end_time", type=float, default=None, help="End time in seconds (cuts chart after this time)")

    g_ui = ap.add_argument_group("UI")
    g_ui.add_argument("--no_title_overlay", action="store_true")
    g_ui.add_argument("--font_path", type=str, default=None)
    g_ui.add_argument("--font_size_multiplier", type=float, default=1.0)

    g_rpe = ap.add_argument_group("RPE")
    g_rpe.add_argument("--rpe_easing_shift", type=int, default=0)

    g_dbg = ap.add_argument_group("Debug")
    g_dbg.add_argument("--basic_debug", action="store_true")
    g_dbg.add_argument("--debug_line_label", action="store_true")
    g_dbg.add_argument("--debug_line_stats", action="store_true")
    g_dbg.add_argument("--debug_judge_windows", action="store_true")
    g_dbg.add_argument("--debug_note_info", action="store_true")
    g_dbg.add_argument("--debug_particles", action="store_true")
    g_dbg.add_argument("--hit_debug", action="store_true")

    args = ap.parse_args()

    setup_logging(args)
    logger.debug("CLI args parsed")

    try:
        setattr(state, "_sigint", False)
    except:
        pass
    try:
        def _on_sigint(_signum, _frame):
            try:
                setattr(state, "_sigint", True)
            except:
                pass
        _old_sigint = signal.getsignal(signal.SIGINT)
        signal.signal(signal.SIGINT, _on_sigint)
    except:
        _old_sigint = None

    cfg_mods: Optional[Dict[str, Any]] = None

    if (not args.input) and (not args.advance) and (not getattr(args, "playlist_script", None)):
        raise SystemExit("Either --input or --advance or --playlist_script must be provided")

    cfg_v2_raw: Optional[Dict[str, Any]] = None
    if args.config:
        try:
            cfg_v2_raw = load_config_v2(str(args.config))
            flat_cfg, mods_cfg = flatten_config_v2(cfg_v2_raw)
            cfg_mods = mods_cfg
            for k, v in (flat_cfg or {}).items():
                if not hasattr(args, k):
                    continue
                if ("--" + k) in sys.argv:
                    continue
                setattr(args, k, v)
        except:
            pass

    # language: CLI overrides config ui.lang
    lang = getattr(args, "lang", None)
    if not lang:
        lang = pick_lang_from_config(cfg_v2_raw)
    lang = normalize_lang(lang)

    if args.config_old:
        try:
            with open(str(args.config_old), "r", encoding="utf-8") as f:
                cfg_old = json.load(f)
            if isinstance(cfg_old, dict):
                cfg_mods = cfg_old.get("mods")
            for k, v in (cfg_old or {}).items():
                if not hasattr(args, k):
                    continue
                if ("--" + k) in sys.argv:
                    continue
                setattr(args, k, v)
        except:
            pass

    if args.save_config:
        try:
            with open(args.save_config, "w", encoding="utf-8") as f:
                f.write(dump_config_v2(args, mods=cfg_mods, lang=lang))
        except:
            pass

    def _c(s: str, code: str) -> str:
        if bool(getattr(args, "no_color", False)):
            return s
        return f"\x1b[{code}m{s}\x1b[0m"

    def _kv(k: str, v: Any) -> str:
        if v is None:
            v = tr(lang, "cui.value.none", "(none)")
        return f"{_c(k, '1;36')}: {v}"

    if not bool(getattr(args, "quiet", False)):
        title = tr(lang, "cui.title", "Mini Phigros Renderer")
        logger.info(title)
        if getattr(args, "config", None):
            logger.info(_kv(tr(lang, "cui.config", "Config"), str(getattr(args, "config"))))
        elif getattr(args, "config_old", None):
            logger.info(_kv(tr(lang, "cui.config", "Config"), str(getattr(args, "config_old"))))
        logger.info(_kv(tr(lang, "cui.input", "Input"), getattr(args, "input", None) or getattr(args, "advance", None)))
        logger.info(_kv(tr(lang, "cui.window", "Window"), f"{int(args.w)}x{int(args.h)}"))
        logger.info(_kv(tr(lang, "cui.backend", "Backend"), f"{getattr(args, 'backend', None)} / {getattr(args, 'audio_backend', None)}"))
        logger.info(_kv(tr(lang, "cui.assets", "Assets"), f"respack={getattr(args, 'respack', None)} bg={getattr(args, 'bg', None)}"))
        logger.info(_kv(tr(lang, "cui.render", "Render"), f"speed={getattr(args, 'chart_speed', None)} approach={getattr(args, 'approach', None)} expand={getattr(args, 'expand', None)}"))
        ap_on = tr(lang, "cui.on", "on") if bool(getattr(args, "autoplay", False)) else tr(lang, "cui.off", "off")
        logger.info(_kv(tr(lang, "cui.autoplay", "Autoplay"), ap_on))
        logger.info(_kv(tr(lang, "cui.start_end", "Start/End"), f"{getattr(args, 'start_time', None)} .. {getattr(args, 'end_time', None)}"))
        logger.info(tr(lang, "cui.help.hint", "Tip: --save_config writes a commented config template."))

    W, H = args.w, args.h
    expand = float(args.expand) if args.expand is not None else 1.0
    if expand <= 1.000001:
        expand = 1.0

    # Visibility checks (shared across modules)
    state.expand_factor = expand

    try:
        state.note_flow_speed_multiplier = float(getattr(args, "note_flow_speed_multiplier", 1.0) or 1.0)
    except:
        state.note_flow_speed_multiplier = 1.0

    try:
        state.note_scale_x = float(getattr(args, "note_scale_x", 1.0) or 1.0)
        state.note_scale_y = float(getattr(args, "note_scale_y", 1.0) or 1.0)
    except:
        state.note_scale_x = 1.0
        state.note_scale_y = 1.0

    # RPE easingType shift (some exporters are 1-based)
    easing.set_rpe_easing_shift(int(args.rpe_easing_shift))

    if getattr(args, "playlist_script", None):
        run_playlist_script(args)
        return

    adv = load_from_args(args, W, H)

    fmt = adv.fmt
    offset = adv.offset
    lines = adv.lines
    notes = adv.notes
    advance_active = adv.advance_active
    advance_cfg = adv.advance_cfg
    advance_mix = adv.advance_mix
    advance_tracks_bgm = adv.advance_tracks_bgm
    advance_main_bgm = adv.advance_main_bgm
    advance_segment_starts = adv.advance_segment_starts
    advance_segment_bgm = adv.advance_segment_bgm
    advance_base_dir = adv.advance_base_dir
    advance_mods = adv.advance_mods
    chart_path = adv.chart_path
    chart_info = adv.chart_info
    bg_path = adv.bg_path
    music_path = adv.music_path
    bg_dim_alpha = adv.bg_dim_alpha

    mods_cfg: Dict[str, Any] = {}
    if isinstance(cfg_mods, dict):
        mods_cfg.update(cfg_mods)
    if advance_active and advance_cfg is not None and isinstance(locals().get("advance_mods"), dict):
        mods_cfg.update(locals()["advance_mods"])

    notes = apply_mods(mods_cfg, notes, lines)

    try:
        run_renderer(
            args,
            W=W,
            H=H,
            expand=expand,
            fmt=fmt,
            offset=offset,
            lines=lines,
            notes=notes,
            chart_info=chart_info,
            bg_dim_alpha=bg_dim_alpha,
            bg_path=bg_path,
            music_path=music_path,
            chart_path=chart_path,
            advance_active=advance_active,
            advance_cfg=advance_cfg,
            advance_mix=advance_mix,
            advance_tracks_bgm=advance_tracks_bgm,
            advance_main_bgm=advance_main_bgm,
            advance_segment_starts=advance_segment_starts,
            advance_segment_bgm=advance_segment_bgm,
            advance_base_dir=advance_base_dir,
        )
    except KeyboardInterrupt:
        try:
            logger.info("[phic_renderer] Interrupted")
        except:
            pass
    finally:
        if _old_sigint is not None:
            try:
                signal.signal(signal.SIGINT, _old_sigint)
            except:
                pass
