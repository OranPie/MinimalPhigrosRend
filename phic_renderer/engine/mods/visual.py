from __future__ import annotations

from typing import Any, Dict

from ... import state
from ...math.util import clamp
from .base import parse_float, parse_int, parse_alpha01


def apply_visual_mods(mods_cfg: Dict[str, Any]):
    gla = mods_cfg.get("force_line_alpha", None)
    if gla is not None:
        a01 = parse_alpha01(gla)
        if a01 is not None:
            state.force_line_alpha01 = a01

    if bool(mods_cfg.get("note_speed_mul_affects_travel", False)):
        state.note_speed_mul_affects_travel = True

    visual_cfg = None
    for k in ("visual", "render", "renderer"):
        if k in mods_cfg and isinstance(mods_cfg.get(k), dict):
            visual_cfg = mods_cfg.get(k)
            break

    if isinstance(visual_cfg, dict) and visual_cfg:
        ov = parse_float(visual_cfg.get("overrender", visual_cfg.get("render_scale", None)))
        if ov is not None:
            state.render_overrender = max(1.0, float(ov))

        trail_cfg = visual_cfg.get("trail", None)
        if isinstance(trail_cfg, dict) and bool(trail_cfg.get("enable", True)):
            a = parse_float(trail_cfg.get("alpha", trail_cfg.get("trail_alpha", None)))
            if a is not None:
                state.trail_alpha = clamp(float(a), 0.0, 1.0)
            fr = parse_int(trail_cfg.get("frames", trail_cfg.get("trail_frames", None)))
            if fr is not None:
                state.trail_frames = max(1, int(fr))
            dec = parse_float(trail_cfg.get("decay", None))
            if dec is not None:
                state.trail_decay = clamp(float(dec), 0.0, 1.0)
            bl = parse_int(trail_cfg.get("blur", trail_cfg.get("trail_blur", None)))
            if bl is not None:
                state.trail_blur = max(0, int(bl))
            dm = parse_int(trail_cfg.get("dim", trail_cfg.get("trail_dim", None)))
            if dm is not None:
                state.trail_dim = clamp(int(dm), 0, 255)
            if "blur_ramp" in trail_cfg:
                try:
                    state.trail_blur_ramp = bool(trail_cfg.get("blur_ramp"))
                except:
                    pass
            if "blend" in trail_cfg and trail_cfg.get("blend") is not None:
                try:
                    state.trail_blend = str(trail_cfg.get("blend")).strip().lower()
                except:
                    pass

        mb_cfg = visual_cfg.get("motion_blur", visual_cfg.get("motionblur", None))
        if isinstance(mb_cfg, dict) and bool(mb_cfg.get("enable", True)):
            smp = parse_int(mb_cfg.get("samples", mb_cfg.get("n", None)))
            if smp is not None:
                state.motion_blur_samples = max(1, int(smp))
            shu = parse_float(mb_cfg.get("shutter", None))
            if shu is not None:
                state.motion_blur_shutter = clamp(float(shu), 0.0, 2.0)

    ov2 = parse_float(mods_cfg.get("overrender", None))
    if ov2 is not None:
        state.render_overrender = max(1.0, float(ov2))

    a2 = parse_float(mods_cfg.get("trail_alpha", None))
    if a2 is not None:
        state.trail_alpha = clamp(float(a2), 0.0, 1.0)
    fr2 = parse_int(mods_cfg.get("trail_frames", None))
    if fr2 is not None:
        state.trail_frames = max(1, int(fr2))
    dec2 = parse_float(mods_cfg.get("trail_decay", None))
    if dec2 is not None:
        state.trail_decay = clamp(float(dec2), 0.0, 1.0)
    bl2 = parse_int(mods_cfg.get("trail_blur", None))
    if bl2 is not None:
        state.trail_blur = max(0, int(bl2))
    dm2 = parse_int(mods_cfg.get("trail_dim", None))
    if dm2 is not None:
        state.trail_dim = clamp(int(dm2), 0, 255)
    if "trail_blur_ramp" in mods_cfg:
        try:
            state.trail_blur_ramp = bool(mods_cfg.get("trail_blur_ramp"))
        except:
            pass
    if "trail_blend" in mods_cfg and mods_cfg.get("trail_blend") is not None:
        try:
            state.trail_blend = str(mods_cfg.get("trail_blend")).strip().lower()
        except:
            pass

    mb_s = parse_int(mods_cfg.get("motion_blur_samples", mods_cfg.get("mb_samples", None)))
    if mb_s is not None:
        state.motion_blur_samples = max(1, int(mb_s))
    mb_sh = parse_float(mods_cfg.get("motion_blur_shutter", mods_cfg.get("mb_shutter", None)))
    if mb_sh is not None:
        state.motion_blur_shutter = clamp(float(mb_sh), 0.0, 2.0)
