from __future__ import annotations

from typing import Any

from .. import state
from ..audio import create_audio_backend
from ..pygame.resources.audio import HitsoundPlayer


def run(args: Any, **ctx: Any):
    try:
        import pygame
    except:
        raise SystemExit("Pygame is required for the moderngl backend window/context creation.")

    W = int(ctx.get("W") or getattr(args, "w", 1280))
    H = int(ctx.get("H") or getattr(args, "h", 720))

    pygame.init()
    try:
        pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MAJOR_VERSION, 3)
        pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MINOR_VERSION, 3)
        pygame.display.gl_set_attribute(pygame.GL_CONTEXT_PROFILE_MASK, pygame.GL_CONTEXT_PROFILE_CORE)
        pygame.display.gl_set_attribute(pygame.GL_CONTEXT_FORWARD_COMPATIBLE_FLAG, 1)
        pygame.display.gl_set_attribute(pygame.GL_DOUBLEBUFFER, 1)
        pygame.display.gl_set_attribute(pygame.GL_DEPTH_SIZE, 24)
        pygame.display.gl_set_attribute(pygame.GL_STENCIL_SIZE, 8)
    except Exception:
        pass
    pygame.display.set_mode((W, H), pygame.OPENGL | pygame.DOUBLEBUF)
    pygame.display.set_caption("Mini Phigros Renderer (ModernGL)")
    clock = pygame.time.Clock()

    from .moderngl.context import create_context
    from .moderngl.app import create_app
    from .moderngl.loop import run_loop
    from .moderngl.respack_loader import load_respack
    from .moderngl.texture import load_texture_rgba
    from ..runtime.visibility import precompute_t_enter

    audio = create_audio_backend(getattr(args, "audio_backend", "pygame"))

    glctx = create_context()
    rc = dict(ctx)

    # BGM seek: align start_time/end_time with audio timebase for non-advance charts.
    use_bgm_clock = False
    start_time_sec = 0.0
    if (not bool(ctx.get("advance_active", False))) and getattr(args, "start_time", None) is not None:
        try:
            start_time_sec = float(getattr(args, "start_time"))
        except:
            start_time_sec = 0.0
    chart_speed = float(getattr(args, "chart_speed", 1.0) or 1.0)
    if chart_speed <= 1e-9:
        chart_speed = 1.0
    offset = float(ctx.get("offset", 0.0) or 0.0)
    music_start_pos_sec = float(offset) + float(start_time_sec) / float(chart_speed)
    if music_start_pos_sec < 0.0:
        music_start_pos_sec = 0.0

    bgm_file = ctx.get("music_path", None)
    if bool(getattr(args, "force", False)) and getattr(args, "bgm", None):
        bgm_file = getattr(args, "bgm")
    if bgm_file:
        try:
            audio.play_music_file(
                str(bgm_file),
                volume=float(getattr(args, "bgm_volume", 0.8) or 0.8),
                start_pos_sec=float(music_start_pos_sec),
            )
            use_bgm_clock = True
        except:
            use_bgm_clock = False

    rc["audio"] = audio
    rc["use_bgm_clock"] = bool(use_bgm_clock)
    rc["music_start_pos_sec"] = float(music_start_pos_sec)

    chart_path = ctx.get("chart_path", None)
    advance_active = bool(ctx.get("advance_active", False))
    advance_base_dir = ctx.get("advance_base_dir", None)
    import os
    chart_dir = os.path.dirname(os.path.abspath(chart_path)) if chart_path else ((advance_base_dir or os.getcwd()) if advance_active else os.getcwd())
    rc["chart_dir"] = chart_dir

    # Precompute first entry time for each note (used by no_cull / culling in renderer).
    try:
        precompute_t_enter(rc.get("lines") or [], rc.get("notes") or [], W, H)
    except:
        pass

    # Background image (no blur for ModernGL backend, but keep dim behavior consistent).
    rc["bg_dim_alpha"] = ctx.get("bg_dim_alpha", None)
    bg_file = getattr(args, "bg", None) if getattr(args, "bg", None) else ctx.get("bg_path", None)
    if bg_file and (not os.path.isabs(str(bg_file))):
        cand = os.path.join(chart_dir, str(bg_file))
        if os.path.exists(cand):
            bg_file = cand
    if bg_file and os.path.exists(str(bg_file)):
        try:
            rc["bg_tex"] = load_texture_rgba(glctx, str(bg_file), flip_y=True)
        except:
            rc["bg_tex"] = None

    hitsound_min_interval_ms = max(0, int(getattr(args, "hitsound_min_interval_ms", 30)))
    rc["hitsound"] = HitsoundPlayer(audio=audio, chart_dir=chart_dir, min_interval_ms=hitsound_min_interval_ms)
    if getattr(args, "respack", None):
        try:
            rp = load_respack(str(args.respack), glctx=glctx, audio=audio)
            state.respack = rp
            rc["respack"] = rp
        except:
            state.respack = None

    app = create_app(glctx, window_size=(W, H), args=args, render_ctx=rc)
    try:
        run_loop(pygame=pygame, clock=clock, screen=pygame.display.get_surface(), app=app)
    except KeyboardInterrupt:
        pass

    try:
        audio.close()
    except:
        pass

    pygame.quit()
