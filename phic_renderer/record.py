from __future__ import annotations

import argparse
import os
import sys
from typing import Any, Dict, Optional

from . import state
from .config_v2 import flatten_config_v2, load_config_v2
from .runtime.advance import load_from_args
from .runtime.mods import apply_mods
from .renderer import run as run_renderer


def main():
    ap = argparse.ArgumentParser(prog="phic_renderer.record")

    g_in = ap.add_argument_group("Input")
    g_in.add_argument("--input", required=False, default=None, help="chart.json OR chart pack folder OR .zip/.pez pack")
    g_in.add_argument("--advance", type=str, default=None)

    g_cfg = ap.add_argument_group("Config")
    g_cfg.add_argument("--config", type=str, required=True, help="Config v2 (JSONC) path")

    g_rec = ap.add_argument_group("Record")
    g_rec.add_argument("--out_dir", type=str, required=True, help="Output directory for frame_XXXXXX.png")
    g_rec.add_argument("--fps", type=float, default=60.0)
    g_rec.add_argument("--start_time", type=float, default=0.0)
    g_rec.add_argument("--end_time", type=float, default=None)
    g_rec.add_argument("--headless", action="store_true", help="Do not open a window; render as fast as possible with CLI progress")
    g_rec.add_argument("--log_interval", type=float, default=1.0, help="Seconds between CLI incoming/past logs (0 disables)")
    g_rec.add_argument("--log_notes", action="store_true", help="Log incoming/past note counts periodically")

    args = ap.parse_args()

    rec_out_dir = str(args.out_dir)
    rec_fps = float(args.fps)
    rec_start_time = float(args.start_time)
    rec_end_time = float(args.end_time) if args.end_time is not None else None
    rec_headless = bool(getattr(args, "headless", False))
    rec_log_interval = float(getattr(args, "log_interval", 1.0) or 0.0)
    rec_log_notes = bool(getattr(args, "log_notes", False))

    if (not args.input) and (not args.advance):
        raise SystemExit("Either --input or --advance must be provided")

    cfg_v2_raw: Optional[Dict[str, Any]] = None
    try:
        cfg_v2_raw = load_config_v2(str(args.config))
    except Exception as e:
        raise SystemExit(f"Failed to load config: {args.config} ({e})")

    flat_cfg, mods_cfg = flatten_config_v2(cfg_v2_raw)

    # Build a minimal args object that renderer expects. Main parameters come from config only.
    # Do not provide CLI overrides for them.
    for k, v in (flat_cfg or {}).items():
        setattr(args, k, v)

    # Force pygame backend for now (recording is implemented in pygame backend).
    setattr(args, "backend", "pygame")

    # Recording options consumed by pygame_backend.run()
    setattr(args, "record_dir", rec_out_dir)
    setattr(args, "record_fps", rec_fps)
    setattr(args, "record_start_time", rec_start_time)
    setattr(args, "record_end_time", rec_end_time)
    setattr(args, "record_headless", rec_headless)
    setattr(args, "record_log_interval", rec_log_interval)
    setattr(args, "record_log_notes", rec_log_notes)

    try:
        os.makedirs(rec_out_dir, exist_ok=True)
    except Exception as e:
        raise SystemExit(f"Cannot create output directory: {rec_out_dir} ({e})")

    W = int(getattr(args, "w", 1280) or 1280)
    H = int(getattr(args, "h", 720) or 720)
    expand = float(getattr(args, "expand", 1.0) or 1.0)
    if expand <= 1.000001:
        expand = 1.0

    state.expand_factor = float(expand)

    adv = load_from_args(args, W, H)

    fmt = adv.fmt
    offset = adv.offset
    lines = adv.lines
    notes = adv.notes

    mods_cfg_out: Dict[str, Any] = {}
    if isinstance(mods_cfg, dict):
        mods_cfg_out.update(mods_cfg)
    if adv.advance_active and isinstance(adv.advance_mods, dict):
        mods_cfg_out.update(adv.advance_mods)

    notes = apply_mods(mods_cfg_out, notes, lines)

    run_renderer(
        args,
        W=W,
        H=H,
        expand=expand,
        fmt=fmt,
        offset=offset,
        lines=lines,
        notes=notes,
        chart_info=adv.chart_info,
        bg_dim_alpha=adv.bg_dim_alpha,
        bg_path=adv.bg_path,
        music_path=adv.music_path,
        chart_path=adv.chart_path,
        advance_active=adv.advance_active,
        advance_cfg=adv.advance_cfg,
        advance_mix=adv.advance_mix,
        advance_tracks_bgm=adv.advance_tracks_bgm,
        advance_main_bgm=adv.advance_main_bgm,
        advance_segment_starts=adv.advance_segment_starts,
        advance_segment_bgm=adv.advance_segment_bgm,
        advance_base_dir=adv.advance_base_dir,
    )


if __name__ == "__main__":
    main()
