from __future__ import annotations

import argparse
import os
import signal
import sys
import tempfile
from typing import Any, Dict, Optional

from . import state
from .config_v2 import flatten_config_v2, load_config_v2
from .runtime.advance import load_from_args
from .runtime.mods import apply_mods
from .renderer import run as run_renderer
from .recording.frame_recorder import FrameRecorder
from .recording.video_recorder import VideoRecorder, check_ffmpeg
from .recording.audio_mixer import mix_wav
from .respack import load_respack_info
from .recording.presets import list_presets


def main():
    ap = argparse.ArgumentParser(prog="phic_renderer.record")

    g_in = ap.add_argument_group("Input")
    g_in.add_argument("--input", required=False, default=None, help="chart.json OR chart pack folder OR .zip/.pez pack")
    g_in.add_argument("--advance", type=str, default=None)

    g_cfg = ap.add_argument_group("Config")
    g_cfg.add_argument("--config", type=str, required=True, help="Config v2 (JSONC) path")

    g_rec = ap.add_argument_group("Record")
    g_rec.add_argument("--out_dir", type=str, default=None, help="Output directory for frame_XXXXXX.png (frames mode)")
    g_rec.add_argument("--output", type=str, default=None, help="Output video file path (video/video+audio modes)")
    g_rec.add_argument("--mode", type=str, default="video+audio", choices=["frames", "video", "video+audio"],
                      help="Recording mode: frames (PNG sequence), video (MP4 no audio), video+audio (MP4 with audio)")
    g_rec.add_argument("--preset", type=str, default="balanced", choices=list_presets(),
                      help="Encoding preset: fast, balanced, quality, archive")
    g_rec.add_argument("--codec", type=str, default="libx264", help="Video codec (libx264, libx265, libvpx-vp9)")
    g_rec.add_argument("--fps", type=float, default=60.0)
    g_rec.add_argument("--start_time", type=float, default=0.0)
    g_rec.add_argument("--end_time", type=float, default=None)
    g_rec.add_argument("--duration", type=float, default=None, help="Seconds to record (alternative to --end_time)")
    g_rec.add_argument("--headless", action="store_true", help="Do not open a window; render as fast as possible with CLI progress")
    g_rec.add_argument("--log_interval", type=float, default=1.0, help="Seconds between CLI incoming/past logs (0 disables)")
    g_rec.add_argument("--log_notes", action="store_true", help="Log incoming/past note counts periodically")
    g_rec.add_argument("--no_hitsound", action="store_true", help="Do not include hitsounds in video audio track")
    g_rec.add_argument("--no_curses", action="store_true", help="Disable curses CUI in headless mode")
    g_rec.add_argument("--curses_fps", type=float, default=10.0, help="Headless curses refresh rate")
    g_rec.add_argument("--no_particles", action="store_true", help="Do not render particles into recorded frames")
    g_rec.add_argument("--no_text", action="store_true", help="Do not render text overlays into recorded frames")

    args = ap.parse_args()

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

    # Determine recording mode and validate arguments
    mode = str(args.mode)
    rec_fps = float(args.fps)
    rec_start_time = float(args.start_time)
    rec_end_time = float(args.end_time) if args.end_time is not None else None
    rec_duration = float(args.duration) if args.duration is not None else None
    rec_headless = bool(getattr(args, "headless", False))
    rec_log_interval = float(getattr(args, "log_interval", 1.0) or 0.0)
    rec_log_notes = bool(getattr(args, "log_notes", False))

    if rec_end_time is not None and rec_duration is not None:
        raise SystemExit("Use only one of --end_time or --duration")
    if rec_duration is not None and rec_duration <= 0.0:
        raise SystemExit("--duration must be > 0")
    if rec_end_time is not None and rec_end_time <= rec_start_time:
        raise SystemExit("--end_time must be greater than --start_time")

    # Validate output arguments based on mode
    if mode == "frames":
        if not args.out_dir:
            raise SystemExit("--out_dir is required for frames mode")
        rec_out_dir = str(args.out_dir)
    else:
        if not args.output:
            raise SystemExit("--output is required for video/video+audio modes")
        rec_output = str(args.output)

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

    # Remove line that forces pygame backend - allow moderngl recording in future
    # setattr(args, "backend", "pygame")

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

    # Ensure recording has a finite stop time.
    # Priority:
    # 1) --end_time
    # 2) --duration => end_time = start_time + duration
    # 3) default => record until chart end (chart_end is relative to chart timeline)
    if rec_end_time is None:
        if rec_duration is not None:
            rec_end_time = float(rec_start_time) + float(rec_duration)
        else:
            try:
                chart_end = max((float(getattr(n, "t_end", 0.0) or 0.0) for n in notes if not getattr(n, "fake", False)), default=0.0)
            except:
                chart_end = 0.0
            if chart_end <= 1e-6:
                # Fallback: avoid infinite recording even if chart_end is missing
                chart_end = 30.0
            rec_end_time = max(float(rec_start_time), float(chart_end))

    mods_cfg_out: Dict[str, Any] = {}
    if isinstance(mods_cfg, dict):
        mods_cfg_out.update(mods_cfg)
    if adv.advance_active and isinstance(adv.advance_mods, dict):
        mods_cfg_out.update(adv.advance_mods)

    notes = apply_mods(mods_cfg_out, notes, lines)

    audio_path: Optional[str] = None
    audio_temp_fd: Optional[int] = None
    audio_temp_keep: Optional[str] = None
    respack_tmpdir = None

    if mode in ["video", "video+audio"]:
        if not check_ffmpeg():
            raise SystemExit("ERROR: ffmpeg not found. Please install ffmpeg for video recording.\n"
                           "Install: https://ffmpeg.org/download.html")

        if mode == "video+audio":
            try:
                duration = max(0.0, float(rec_end_time) - float(rec_start_time))
                if duration <= 1e-6:
                    raise RuntimeError("Invalid recording duration")

                bgm_tracks = []
                bgm_vol = float(getattr(args, "bgm_volume", 0.8) or 0.8)

                if adv.advance_active:
                    if adv.advance_tracks_bgm:
                        for tr in adv.advance_tracks_bgm:
                            p = str(tr.get("path") or "")
                            if not p or (not os.path.exists(p)):
                                continue
                            st = float(tr.get("start_at", 0.0) or 0.0)
                            en = tr.get("end_at", None)
                            en = float(en) if en is not None else None

                            seg_start = max(float(rec_start_time), st)
                            seg_end = float(rec_end_time) if en is None else min(float(rec_end_time), float(en))
                            seg_dur = max(0.0, seg_end - seg_start)
                            if seg_dur <= 1e-6:
                                continue

                            start_at_out = seg_start - float(rec_start_time)
                            ss = max(0.0, float(rec_start_time) - st)
                            bgm_tracks.append((p, start_at_out, seg_dur, bgm_vol, ss))
                    elif adv.advance_segment_bgm and adv.advance_segment_starts:
                        starts = list(adv.advance_segment_starts)
                        for i, p in enumerate(list(adv.advance_segment_bgm)):
                            if not p:
                                continue
                            pth = str(p)
                            if not os.path.exists(pth):
                                continue
                            st = float(starts[i]) if i < len(starts) else 0.0
                            nxt = float(starts[i + 1]) if (i + 1) < len(starts) else float(rec_end_time)

                            seg_start = max(float(rec_start_time), st)
                            seg_end = min(float(rec_end_time), nxt)
                            seg_dur = max(0.0, seg_end - seg_start)
                            if seg_dur <= 1e-6:
                                continue
                            start_at_out = seg_start - float(rec_start_time)
                            ss = max(0.0, float(rec_start_time) - st)
                            bgm_tracks.append((pth, start_at_out, seg_dur, bgm_vol, ss))
                else:
                    music_path = None
                    if bool(getattr(args, "force", False)) and getattr(args, "bgm", None):
                        music_path = str(getattr(args, "bgm"))
                    elif adv.music_path and os.path.exists(str(adv.music_path)):
                        music_path = str(adv.music_path)
                    elif getattr(args, "bgm", None):
                        music_path = str(getattr(args, "bgm"))

                    if music_path and os.path.exists(music_path):
                        chart_speed = float(getattr(args, "chart_speed", 1.0) or 1.0)
                        if chart_speed <= 1e-9:
                            chart_speed = 1.0
                        ss0 = float(adv.offset) + float(rec_start_time) / float(chart_speed)
                        bgm_tracks.append((
                            str(music_path),
                            0.0,
                            float(rec_end_time) - float(rec_start_time),
                            bgm_vol,
                            max(0.0, ss0),
                        ))

                hitsound_events = []
                include_hitsound = not bool(getattr(args, "no_hitsound", False))
                if include_hitsound:
                    respack_zip = getattr(args, "respack", None)
                    respack_sfx = {}
                    if respack_zip and os.path.exists(str(respack_zip)):
                        respack_tmpdir, _info = load_respack_info(str(respack_zip))
                        base = str(getattr(respack_tmpdir, "name", ""))
                        if base:
                            cand = {
                                "click": os.path.join(base, "click.ogg"),
                                "drag": os.path.join(base, "drag.ogg"),
                                "flick": os.path.join(base, "flick.ogg"),
                            }
                            for k, fp in cand.items():
                                if os.path.exists(fp):
                                    respack_sfx[k] = fp

                    for n in notes:
                        if getattr(n, "fake", False):
                            continue
                        t = float(getattr(n, "t_hit", 0.0) or 0.0)
                        t_out = t - float(rec_start_time)
                        if t_out < 0.0 or t_out >= float(duration):
                            continue

                        pth = None
                        hs = getattr(n, "hitsound_path", None)
                        if hs and os.path.exists(str(hs)):
                            pth = str(hs)
                        else:
                            kind = int(getattr(n, "kind", 1) or 1)
                            key = "click"
                            if kind == 2:
                                key = "drag"
                            elif kind == 4:
                                key = "flick"
                            pth = respack_sfx.get(key)
                        if pth:
                            hitsound_events.append((pth, t_out, 1.0))

                audio_temp_fd, audio_temp_keep = tempfile.mkstemp(suffix='.wav', prefix='phigros_mix_')
                os.close(audio_temp_fd)
                audio_path = mix_wav(
                    out_wav_path=audio_temp_keep,
                    duration=duration,
                    bgm_tracks=bgm_tracks,
                    hitsound_events=hitsound_events,
                )
                print("[Recording] Audio: Mixed")
            except Exception as e:
                print(f"[Recording] Warning: Failed to prepare audio: {e}")
                print("[Recording] Falling back to video-only mode")
                audio_path = None

        recorder = VideoRecorder(
            output_file=rec_output,
            width=W,
            height=H,
            fps=rec_fps,
            preset=args.preset,
            audio_path=audio_path,
            codec=args.codec
        )

        print(f"[Recording] Mode: {'Video+Audio' if audio_path else 'Video-only'} → {rec_output}")
        print(f"[Recording] Resolution: {W}x{H} @ {rec_fps}fps")
        print(f"[Recording] Preset: {args.preset} (codec: {args.codec})")

    else:
        recorder = None
        if mode == "frames":
            try:
                os.makedirs(rec_out_dir, exist_ok=True)
            except Exception as e:
                raise SystemExit(f"Cannot create output directory: {rec_out_dir} ({e})")

            recorder = FrameRecorder(rec_out_dir, W, H, rec_fps)
            print(f"[Recording] Mode: PNG frames → {rec_out_dir}")
            print(f"[Recording] Resolution: {W}x{H} @ {rec_fps}fps")

    if recorder:
        try:
            recorder.open()
            print("[Recording] Recorder initialized successfully")
        except Exception as e:
            raise SystemExit(f"Failed to initialize recorder: {e}")

    setattr(args, "recorder", recorder)

    setattr(args, "record_enabled", True)
    setattr(args, "record_fps", rec_fps)
    setattr(args, "record_start_time", rec_start_time)
    setattr(args, "record_end_time", rec_end_time)
    setattr(args, "record_headless", rec_headless)
    setattr(args, "record_log_interval", rec_log_interval)
    setattr(args, "record_log_notes", rec_log_notes)
    setattr(args, "record_use_curses", (not bool(getattr(args, "no_curses", False))) and rec_headless)
    setattr(args, "record_curses_fps", float(getattr(args, "curses_fps", 10.0) or 10.0))
    setattr(args, "record_render_particles", (not bool(getattr(args, "no_particles", False))))
    setattr(args, "record_render_text", (not bool(getattr(args, "no_text", False))))

    interrupted = False
    try:
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
        except KeyboardInterrupt:
            interrupted = True
    finally:
        # Ensure recorder is properly closed
        if recorder:
            try:
                recorder.close()
                print("[Recording] Finalized successfully")
            except Exception as e:
                print(f"[Recording] Warning: Failed to finalize: {e}")

        if interrupted:
            try:
                print("[Recording] Interrupted", flush=True)
            except:
                pass

        if _old_sigint is not None:
            try:
                signal.signal(signal.SIGINT, _old_sigint)
            except:
                pass

        if respack_tmpdir is not None:
            try:
                respack_tmpdir.cleanup()
            except:
                pass

        if audio_temp_keep is not None and os.path.exists(str(audio_temp_keep)):
            try:
                os.remove(str(audio_temp_keep))
            except:
                pass


if __name__ == "__main__":
    main()
