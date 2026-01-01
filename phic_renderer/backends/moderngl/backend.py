from __future__ import annotations

import logging
from typing import Any

from ... import state
from ...audio import create_audio_backend
from ...engine.chart_init import compute_chart_end_policy
from ..pygame.resources.audio import HitsoundPlayer


def run(args: Any, **ctx: Any):
    logger = logging.getLogger(__name__)
    from .session import ModernGLSession
    from ...config.schema import RenderConfig
    from ...core.context import ResourceContext

    try:
        import pygame
    except:
        raise SystemExit("Pygame is required for the moderngl backend window/context creation.")

    W = int(ctx.get("W") or getattr(args, "w", 1280))
    H = int(ctx.get("H") or getattr(args, "h", 720))

    try:
        logger.info("[ModernGL] Init window %dx%d", int(W), int(H))
    except Exception:
        pass

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

    from .context import create_context
    from .app import create_app
    from .loop import run_loop
    from .respack_loader import load_respack
    from .texture import load_texture_rgba
    from ...engine.visibility import precompute_t_enter

    audio = create_audio_backend(getattr(args, "audio_backend", "pygame"))

    try:
        logger.info("[ModernGL] Audio backend: %s", str(getattr(args, "audio_backend", "pygame")))
    except Exception:
        pass

    glctx = create_context()
    try:
        logger.info("[ModernGL] GL context created")
    except Exception:
        pass
    rc = dict(ctx)

    chart_path = ctx.get("chart_path", None)
    advance_active = bool(ctx.get("advance_active", False))
    advance_base_dir = ctx.get("advance_base_dir", None)
    import os
    chart_dir = os.path.dirname(os.path.abspath(chart_path)) if chart_path else ((advance_base_dir or os.getcwd()) if advance_active else os.getcwd())
    rc["chart_dir"] = chart_dir

    # BGM seek: align start_time/end_time with audio timebase for non-advance charts.
    # When transitions are enabled, we defer actually starting audio until intro completes.
    use_bgm_clock = False
    want_bgm_clock = False
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

    bgm_file = None
    if advance_active:
        base_dir = advance_base_dir or chart_dir
        if bool(getattr(args, "force", False)) and getattr(args, "bgm", None):
            bgm_file = getattr(args, "bgm")
        else:
            bgm_file = ctx.get("advance_main_bgm", None)
        if bgm_file and (not os.path.isabs(str(bgm_file))):
            cand = os.path.join(base_dir, str(bgm_file))
            if os.path.exists(cand):
                bgm_file = cand
    else:
        music_path = ctx.get("music_path", None)
        if bool(getattr(args, "force", False)):
            bgm_file = getattr(args, "bgm", None) if getattr(args, "bgm", None) else (music_path if (music_path and os.path.exists(str(music_path))) else None)
        else:
            bgm_file = music_path if (music_path and os.path.exists(str(music_path))) else None
            if not bgm_file:
                bgm_file = getattr(args, "bgm", None)
        if bgm_file and (not os.path.isabs(str(bgm_file))):
            cand = os.path.join(chart_dir, str(bgm_file))
            if os.path.exists(cand):
                bgm_file = cand
    if bgm_file and (not os.path.exists(str(bgm_file))):
        bgm_file = None

    enable_transitions = (not bool(ctx.get("advance_active", False))) and bool(getattr(args, "enable_transitions", True))
    if bgm_file and (not enable_transitions):
        try:
            audio.play_music_file(
                str(bgm_file),
                volume=float(getattr(args, "bgm_volume", 0.8) or 0.8),
                start_pos_sec=float(music_start_pos_sec),
            )
            use_bgm_clock = True
        except:
            use_bgm_clock = False
    elif bgm_file and enable_transitions:
        want_bgm_clock = True
        use_bgm_clock = False

    rc["audio"] = audio
    rc["use_bgm_clock"] = bool(use_bgm_clock)
    rc["want_bgm_clock"] = bool(want_bgm_clock)
    rc["music_start_pos_sec"] = float(music_start_pos_sec)
    rc["start_time_sec"] = float(start_time_sec)
    rc["bgm_file"] = bgm_file

    bgm_duration_sec = None
    if (not bool(ctx.get("advance_active", False))) and bgm_file:
        try:
            import soundfile as sf  # type: ignore

            bgm_duration_sec = float(sf.info(str(bgm_file)).duration)
        except Exception:
            bgm_duration_sec = None
        if bgm_duration_sec is None:
            try:
                import pygame  # type: ignore

                snd = pygame.mixer.Sound(str(bgm_file))
                bgm_duration_sec = float(snd.get_length())
            except Exception:
                bgm_duration_sec = None

    try:
        rc["chart_end"] = compute_chart_end_policy(
            rc.get("notes") or [],
            bool(ctx.get("advance_active", False)),
            (getattr(args, "end_time", None) if (not bool(ctx.get("advance_active", False))) else None),
            bgm_duration_sec=bgm_duration_sec,
            offset=float(offset),
            chart_speed=float(chart_speed),
            no_bgm_tail_sec=2.0,
        )
    except Exception:
        pass

    # Precompute first entry time for each note (used by no_cull / culling in renderer).
    try:
        precompute_t_enter(rc.get("lines") or [], rc.get("notes") or [], W, H)
    except:
        pass

    # Background image (base + optional blur).
    rc["bg_dim_alpha"] = ctx.get("bg_dim_alpha", None)
    bg_file = getattr(args, "bg", None) if getattr(args, "bg", None) else ctx.get("bg_path", None)
    if bg_file and (not os.path.isabs(str(bg_file))):
        cand = os.path.join(chart_dir, str(bg_file))
        if os.path.exists(cand):
            bg_file = cand
    if bg_file and os.path.exists(str(bg_file)):
        try:
            logger.info("[ModernGL] Background: %s", str(bg_file))
            from .resources.background import load_background

            blur_factor = int(getattr(args, "bg_blur", 10) or 0)
            bg_base, bg_blurred = load_background(glctx, str(bg_file), int(W), int(H), blur_factor)
            rc["bg_tex"] = bg_base
            rc["bg_blur_tex"] = bg_blurred
            rc["bg_blur_factor"] = int(blur_factor)
        except:
            rc["bg_tex"] = None
            rc["bg_blur_tex"] = None
            rc["bg_blur_factor"] = int(getattr(args, "bg_blur", 10) or 0)

    hitsound_min_interval_ms = max(0, int(getattr(args, "hitsound_min_interval_ms", 30)))
    rc["hitsound"] = HitsoundPlayer(audio=audio, chart_dir=chart_dir, min_interval_ms=hitsound_min_interval_ms)
    if getattr(args, "respack", None):
        try:
            logger.info("[ModernGL] Respack: %s", str(getattr(args, "respack")))
            rp = load_respack(str(args.respack), glctx=glctx, audio=audio)
            state.respack = rp
            rc["respack"] = rp

            # Option B: auto-enable GPU instanced batching when a respack is loaded.
            try:
                from .resources.texture_manager import create_texture_manager_from_respack
                from .rendering.batch import InstancedBatchRenderer

                logger.info("[ModernGL] Build texture array + instanced batch")
                tm = create_texture_manager_from_respack(glctx, rp)
                br = InstancedBatchRenderer(glctx, tm, max_instances=20000)
                br.set_window_size(int(W), int(H))
                rc["gl_texture_manager"] = tm
                rc["gl_batch_renderer"] = br
            except Exception:
                pass
        except:
            state.respack = None

    config = RenderConfig.from_state_module(state)
    resources = ResourceContext.from_state_module(state)
    try:
        resources.audio_backend = audio
    except Exception:
        pass
    session = ModernGLSession(
        config,
        resources,
        rc.get("lines") or [],
        rc.get("notes") or [],
        rc.get("chart_info") or {},
        args=args,
        pygame=pygame,
        clock=clock,
        glctx=glctx,
        render_ctx=rc,
        create_app=create_app,
        run_loop=run_loop,
        window_size=(W, H),
    )
    try:
        return session.run()
    except KeyboardInterrupt:
        return None
