from __future__ import annotations

import math
import os
from typing import Any, Dict, List, Optional, Tuple

import pygame

from ...core.fx import prune_hitfx
from ...math.util import apply_expand_xy, clamp, rect_corners
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...types import NoteState, RuntimeLine
from .draw import draw_line_rgba, draw_poly_outline_rgba, draw_poly_rgba
from .hitfx import draw_hitfx
from .hold import draw_hold_3slice
from .rendering_helpers import pick_note_image


def render_frame(
    *,
    t_draw: float,
    args: Any,
    state_mod: Any,
    RW: int,
    RH: int,
    W: int,
    H: int,
    expand: float,
    overrender: float,
    surface_pool: Any,
    transform_cache: Any,
    bg_blurred: Optional[pygame.Surface],
    bg_dim_alpha: Optional[int],
    bg_scaled_cache_key: Optional[Tuple[int, int, int]],
    bg_scaled_cache: Optional[pygame.Surface],
    dim_surf_cache_key: Optional[Tuple[int, int, int]],
    dim_surf_cache: Optional[pygame.Surface],
    lines: List[RuntimeLine],
    states: List[NoteState],
    idx_next: int,
    base_note_w: int,
    base_note_h: int,
    note_scale_x: float,
    note_scale_y: float,
    hold_body_w: int,
    outline_w: int,
    line_w: int,
    dot_r: int,
    line_len: int,
    chart_dir: str,
    line_tex_cache: Dict[str, pygame.Surface],
    small: Any,
    note_dbg_cache: Dict[str, pygame.Surface],
    last_debug_ms: int,
    line_last_hit_ms: Dict[int, int],
    respack: Any,
    hitfx: List[Any],
    bad_ghosts: List[Dict[str, Any]],
    MISS_FADE_SEC: float,
    BAD_GHOST_SEC: float,
) -> Tuple[
    pygame.Surface,
    List[Tuple[int, pygame.Surface, float, float]],
    int,
    int,
    Optional[Tuple[int, int, int]],
    Optional[pygame.Surface],
    Optional[Tuple[int, int, int]],
    Optional[pygame.Surface],
]:
    base = surface_pool.get(int(RW), int(RH), pygame.SRCALPHA)
    if bg_blurred:
        key = (id(bg_blurred), int(RW), int(RH))
        if bg_scaled_cache is None or bg_scaled_cache_key != key:
            bg_scaled_cache = pygame.transform.smoothscale(bg_blurred, (int(RW), int(RH)))
            bg_scaled_cache_key = key
        base.blit(bg_scaled_cache, (0, 0))
    else:
        base.fill((10, 10, 14))

    dim = bg_dim_alpha if (bg_dim_alpha is not None) else clamp(getattr(args, "bg_dim", 120), 0, 255)
    if dim > 0:
        dkey = (int(RW), int(RH), int(dim))
        if dim_surf_cache is None or dim_surf_cache_key != dkey:
            dim_surf_cache = pygame.Surface((int(RW), int(RH)), pygame.SRCALPHA)
            dim_surf_cache.fill((0, 0, 0, int(dim)))
            dim_surf_cache_key = dkey
        base.blit(dim_surf_cache, (0, 0))

    overlay = surface_pool.get(int(RW), int(RH), pygame.SRCALPHA)

    line_text_draw_calls: List[Tuple[int, pygame.Surface, float, float]] = []

    line_states: List[Tuple[float, float, float, float, float, float]] = []
    line_trig: List[Tuple[float, float]] = []
    for ln in lines:
        lx, ly, lr, la01, sc, la_raw = eval_line_state(ln, float(t_draw))
        line_states.append((lx, ly, lr, la01, sc, la_raw))
        line_trig.append((math.cos(lr), math.sin(lr)))

    try:
        flow_mul = float(getattr(state_mod, "note_flow_speed_multiplier", 1.0) or 1.0)
    except Exception:
        flow_mul = 1.0
    hold_keep_head = bool(state_mod.respack and getattr(state_mod.respack, "hold_keep_head", False))
    speed_mul_affects_travel = bool(getattr(state_mod, "note_speed_mul_affects_travel", False))

    # Draw judge lines
    for ln, (lx, ly, lr, la01, _sc, _la_raw) in zip(lines, line_states):
        if la01 <= 1e-6:
            continue

        if getattr(ln, "text", None) is not None:
            try:
                s = str(ln.text.eval(float(t_draw)) if hasattr(ln.text, "eval") else "")
            except Exception:
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
                except Exception:
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
                    except Exception:
                        img = None
            if img is not None:
                try:
                    ax, ay = getattr(ln, "anchor", (0.5, 0.5))
                    ax = float(ax)
                    ay = float(ay)
                except Exception:
                    ax, ay = 0.5, 0.5
                sx_tex = 1.0
                sy_tex = 1.0
                try:
                    if getattr(ln, "scale_x", None) is not None:
                        sx_tex = float(ln.scale_x.eval(float(t_draw)) if hasattr(ln.scale_x, "eval") else 1.0)
                except Exception:
                    sx_tex = 1.0
                try:
                    if getattr(ln, "scale_y", None) is not None:
                        sy_tex = float(ln.scale_y.eval(float(t_draw)) if hasattr(ln.scale_y, "eval") else 1.0)
                except Exception:
                    sy_tex = 1.0
                iw, ih = img.get_width(), img.get_height()
                target_w = max(1, int((float(line_len) * float(sx_tex)) * float(overrender) / float(expand)))
                target_h = max(1, int((target_w * ih / max(1, iw)) * float(sy_tex)))

                # Cache smoothscale operation
                img_id = id(img)
                scaled = transform_cache.get_scaled(img, target_w, target_h, img_id)
                if scaled is None:
                    scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                    transform_cache.put_scaled(img, target_w, target_h, img_id, scaled)

                # Cache rotation operation
                angle_deg = -float(lr) * 180.0 / math.pi
                scaled_id = id(scaled)
                rotated = transform_cache.get_rotated(scaled, angle_deg, scaled_id)
                if rotated is None:
                    rotated = pygame.transform.rotate(scaled, angle_deg)
                    transform_cache.put_rotated(scaled, angle_deg, scaled_id, rotated)

                rotated.set_alpha(int(255 * la01))
                axc = (float(ax) - 0.5) * float(target_w)
                ayc = (float(ay) - 0.5) * float(target_h)
                c0 = math.cos(-float(lr))
                s0 = math.sin(-float(lr))
                dx = c0 * axc - s0 * ayc
                dy = s0 * axc + c0 * ayc
                cx, cy = apply_expand_xy(float(lx) * float(overrender), float(ly) * float(overrender), int(RW), int(RH), float(expand))
                overlay.blit(rotated, (cx - rotated.get_width() / 2 - dx, cy - rotated.get_height() / 2 - dy))
                continue

        sx = 1.0
        sy = 1.0
        try:
            if getattr(ln, "scale_x", None) is not None:
                sx = float(ln.scale_x.eval(float(t_draw)) if hasattr(ln.scale_x, "eval") else 1.0)
        except Exception:
            sx = 1.0
        try:
            if getattr(ln, "scale_y", None) is not None:
                sy = float(ln.scale_y.eval(float(t_draw)) if hasattr(ln.scale_y, "eval") else 1.0)
        except Exception:
            sy = 1.0

        if sx <= 1e-6:
            sx = 1.0
        if sy <= 1e-6:
            sy = 1.0

        tx, ty = math.cos(float(lr)), math.sin(float(lr))
        ex = tx * (float(line_len) * float(sx)) * 0.5
        ey = ty * (float(line_len) * 0.5)
        p0 = (float(lx) - float(ex), float(ly) - float(ey))
        p1 = (float(lx) + float(ex), float(ly) + float(ey))
        p0s = apply_expand_xy(p0[0] * float(overrender), p0[1] * float(overrender), int(RW), int(RH), float(expand))
        p1s = apply_expand_xy(p1[0] * float(overrender), p1[1] * float(overrender), int(RW), int(RH), float(expand))
        rgba = (*ln.color_rgb, int(255 * la01))
        draw_line_rgba(overlay, p0s, p1s, rgba, width=int(line_w))
        lxs, lys = apply_expand_xy(float(lx) * float(overrender), float(ly) * float(overrender), int(RW), int(RH), float(expand))
        pygame.draw.circle(overlay, (*ln.color_rgb, int(220 * la01)), (int(lxs), int(lys)), int(dot_r))

        pr = int(line_last_hit_ms.get(ln.lid, 0))
        if getattr(args, "debug_line_label", False):
            label = ln.name.strip() if ln.name.strip() else str(ln.lid)
            txt = small.render(label, True, (240, 240, 240))
            lxs, lys = apply_expand_xy(float(lx) * float(overrender), float(ly) * float(overrender), int(RW), int(RH), float(expand))
            line_text_draw_calls.append((pr, txt, (lxs - txt.get_width() / 2) / float(overrender), (lys - txt.get_height() / 2) / float(overrender)))

    # draw notes
    note_render_count = 0
    note_dbg_drawn = 0
    no_cull_all = bool(getattr(args, "no_cull", False))
    no_cull_screen = bool(getattr(args, "no_cull_screen", False))
    no_cull_enter_time = bool(getattr(args, "no_cull_enter_time", False))
    st0 = max(0, int(idx_next) - 400)
    st1 = min(len(states), int(idx_next) + 1200)
    for si in range(int(st0), int(st1)):
        s = states[si]
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
            if float(t_draw) < float(n.t_enter):
                continue
            t_end_for_cull = float(n.t_end) if int(n.kind) == 3 else float(n.t_hit)
            extra_after = max(0.25, float(getattr(args, "approach", 3.0)) + 0.5)
            if int(n.kind) == 3:
                extra_after = 0.35
            if float(t_draw) > float(t_end_for_cull) + float(extra_after):
                continue

        note_render_count += 1

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
                except Exception:
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
                head = (
                    float(lx) + float(tx) * x_local + float(nx) * y_local,
                    float(ly) + float(ty) * x_local + float(ny) * y_local,
                )
            else:
                if bool(getattr(s, "hit", False)) or bool(getattr(s, "holding", False)) or (float(t_draw) >= float(n.t_hit)):
                    head_target_scroll = n.scroll_hit if float(sc_now) <= float(n.scroll_hit) else float(sc_now)
                else:
                    head_target_scroll = n.scroll_hit
                dy = (float(head_target_scroll) - float(sc_now)) * float(flow_mul)
                if hold_keep_head and dy < 0.0:
                    dy = 0.0
                y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy + float(getattr(n, "y_offset_px", 0.0))
                x_local = float(getattr(n, "x_local_px", 0.0))
                head = (
                    float(lx) + float(tx) * x_local + float(nx) * y_local,
                    float(ly) + float(ty) * x_local + float(ny) * y_local,
                )

            dy = (float(getattr(n, "scroll_end", 0.0)) - float(sc_now)) * float(flow_mul)
            mult = max(0.0, float(getattr(n, "speed_mul", 1.0)))
            y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy * mult + float(getattr(n, "y_offset_px", 0.0))
            x_local = float(getattr(n, "x_local_px", 0.0))
            tail = (
                float(lx) + float(tx) * x_local + float(nx) * y_local,
                float(ly) + float(ty) * x_local + float(ny) * y_local,
            )
            head_s = apply_expand_xy(head[0] * float(overrender), head[1] * float(overrender), int(RW), int(RH), float(expand))
            tail_s = apply_expand_xy(tail[0] * float(overrender), tail[1] * float(overrender), int(RW), int(RH), float(expand))

            if (not no_cull_all) and (not no_cull_screen):
                m = int(120 * float(overrender))
                minx = min(float(head_s[0]), float(tail_s[0]))
                maxx = max(float(head_s[0]), float(tail_s[0]))
                miny = min(float(head_s[1]), float(tail_s[1]))
                maxy = max(float(head_s[1]), float(tail_s[1]))
                if maxx < -m or minx > float(RW + m) or maxy < -m or miny > float(RH + m):
                    continue

            hold_alpha = float(note_alpha)
            if s.hold_failed:
                hold_alpha *= 0.35
            mh = bool(getattr(n, "mh", False))
            size_scale = float(getattr(n, "size_px", 1.0) or 1.0)
            note_rgb = getattr(n, "tint_rgb", (255, 255, 255))
            line_rgb = lines[n.line_id].color_rgb
            prog = None
            try:
                if bool(getattr(s, "hit", False)) or bool(getattr(s, "holding", False)) or (float(t_draw) >= float(n.t_hit)):
                    den = float(n.scroll_end) - float(n.scroll_hit)
                    num = float(sc_now) - float(n.scroll_hit)
                    if abs(den) > 1e-6:
                        prog = clamp(num / den, 0.0, 1.0)
                    else:
                        dur_t = float(n.t_end) - float(n.t_hit)
                        if dur_t > 1e-6:
                            prog = clamp((float(t_draw) - float(n.t_hit)) / dur_t, 0.0, 1.0)
            except Exception:
                prog = None

            draw_hold_3slice(
                overlay=overlay,
                head_xy=head_s,
                tail_xy=tail_s,
                line_rot=float(lr),
                alpha01=float(hold_alpha),
                line_rgb=(int(line_rgb[0]), int(line_rgb[1]), int(line_rgb[2])),
                note_rgb=(int(note_rgb[0]), int(note_rgb[1]), int(note_rgb[2])),
                size_scale=float(size_scale),
                mh=bool(mh),
                hold_body_w=max(1, int(float(hold_body_w) * float(overrender))),
                progress=prog,
                draw_outline=(not getattr(args, "no_note_outline", False)),
                outline_width=max(1, int(float(outline_w) * float(overrender))),
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
                        nxv = -math.sin(float(lr))
                        nyv = math.cos(float(lr))
                        side = 1.0 if bool(getattr(n, "above", True)) else -1.0
                        off = (float(hs) * float(overrender) * 0.8 + 14.0 * float(overrender))
                        tx0 = float(head_s[0]) + nxv * off * side
                        ty0 = float(head_s[1]) + nyv * off * side
                        overlay.blit(surf, (int(tx0 - surf.get_width() / 2), int(ty0 - surf.get_height() / 2)))
                        overlay.blit(
                            surf2,
                            (int(tx0 - surf2.get_width() / 2), int(ty0 - surf2.get_height() / 2 + surf.get_height())),
                        )
                        note_dbg_drawn += 1
                    except Exception:
                        pass
        else:
            dy = (float(getattr(n, "scroll_hit", 0.0)) - float(sc_now)) * float(flow_mul)
            mult = 1.0
            if speed_mul_affects_travel:
                mult = max(0.0, float(getattr(n, "speed_mul", 1.0)))
            y_local = (1.0 if bool(getattr(n, "above", True)) else -1.0) * dy * float(mult) + float(getattr(n, "y_offset_px", 0.0))
            x_local = float(getattr(n, "x_local_px", 0.0))
            p = (
                float(lx) + float(tx) * x_local + float(nx) * y_local,
                float(ly) + float(ty) * x_local + float(ny) * y_local,
            )
            ps = apply_expand_xy(p[0] * float(overrender), p[1] * float(overrender), int(RW), int(RH), float(expand))

            if (not no_cull_all) and (not no_cull_screen):
                m = int(120 * float(overrender))
                if (float(ps[0]) < -m) or (float(ps[0]) > float(RW + m)) or (float(ps[1]) < -m) or (float(ps[1]) > float(RH + m)):
                    continue

            img = pick_note_image(n, respack)
            if img is None:
                if miss_dim > 1e-6:
                    g = int(255 * (1.0 - 0.6 * float(miss_dim)))
                    rgba_fill = (g, g, g, int(255 * note_alpha))
                    rgba_outline = (0, 0, 0, int(220 * note_alpha))
                pts = rect_corners(ps[0], ps[1], ws * float(overrender), hs * float(overrender), float(lr))
                draw_poly_rgba(overlay, pts, rgba_fill)
                if not getattr(args, "no_note_outline", False):
                    draw_poly_outline_rgba(overlay, pts, rgba_outline, width=int(outline_w))
            else:
                iw, ih = img.get_width(), img.get_height()
                target_w = max(1, int(ws * float(overrender)))
                target_h = max(1, int(target_w * ih / max(1, iw) * float(note_scale_y)))

                img_id = id(img)
                scaled = transform_cache.get_scaled(img, target_w, target_h, img_id)
                if scaled is None:
                    scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                    transform_cache.put_scaled(img, target_w, target_h, img_id, scaled)

                angle_deg = -float(lr) * 180.0 / math.pi
                scaled_key_id = (int(img_id), int(target_w), int(target_h))
                rotated = transform_cache.get_rotated(scaled, angle_deg, scaled_key_id)
                if rotated is None:
                    rotated = pygame.transform.rotate(scaled, angle_deg)
                    transform_cache.put_rotated(scaled, angle_deg, scaled_key_id, rotated)

                try:
                    trc, tgc, tbc = getattr(n, "tint_rgb", (255, 255, 255))
                    if miss_dim > 1e-6:
                        g = int(220 * (1.0 - 0.7 * float(miss_dim)))
                        trc = int(trc * (1.0 - 0.8 * float(miss_dim)) + g * (0.8 * float(miss_dim)))
                        tgc = int(tgc * (1.0 - 0.8 * float(miss_dim)) + g * (0.8 * float(miss_dim)))
                        tbc = int(tbc * (1.0 - 0.8 * float(miss_dim)) + g * (0.8 * float(miss_dim)))
                    rotated.fill((int(trc), int(tgc), int(tbc), 255), special_flags=pygame.BLEND_RGBA_MULT)
                except Exception:
                    pass
                rotated.set_alpha(int(255 * note_alpha))
                overlay.blit(rotated, (ps[0] - rotated.get_width() / 2, ps[1] - rotated.get_height() / 2))
                pts = rect_corners(ps[0], ps[1], float(target_w), float(target_h), float(lr))
                if not getattr(args, "no_note_outline", False):
                    draw_poly_outline_rgba(overlay, pts, rgba_outline, width=int(outline_w))

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
                        nxv = -math.sin(float(lr))
                        nyv = math.cos(float(lr))
                        side = 1.0 if bool(getattr(n, "above", True)) else -1.0
                        off = (float(hs) * float(overrender) * 0.8 + 14.0 * float(overrender))
                        tx0 = float(ps[0]) + nxv * off * side
                        ty0 = float(ps[1]) + nyv * off * side
                        overlay.blit(surf, (int(tx0 - surf.get_width() / 2), int(ty0 - surf.get_height() / 2)))
                        overlay.blit(
                            surf2,
                            (int(tx0 - surf2.get_width() / 2), int(ty0 - surf2.get_height() / 2 + surf.get_height())),
                        )
                        note_dbg_drawn += 1
                    except Exception:
                        pass

    # hitfx
    hitfx[:] = prune_hitfx(hitfx, float(t_draw), (respack.hitfx_duration if respack else 0.18))
    for fx in hitfx:
        draw_hitfx(
            overlay,
            fx,
            float(t_draw),
            respack=respack,
            W=int(RW),
            H=int(RH),
            expand=float(expand),
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
                nx0 = float(g.get("x", 0.0))
                ny0 = float(g.get("y", 0.0))
                nr = float(g.get("rot", 0.0))
                nn = g.get("note", None)
                if nn is None:
                    continue
                ps = apply_expand_xy(nx0 * float(overrender), ny0 * float(overrender), int(RW), int(RH), float(expand))
                img = pick_note_image(nn, respack)
                ws = float(base_note_w) * float(note_scale_x) * float(getattr(nn, "size_px", 1.0))
                hs = float(base_note_h) * float(note_scale_y) * float(getattr(nn, "size_px", 1.0))
                if img is None:
                    pts = rect_corners(ps[0], ps[1], ws * float(overrender), hs * float(overrender), float(nr))
                    draw_poly_rgba(overlay, pts, (255, 80, 80, int(180 * a01)))
                    if not getattr(args, "no_note_outline", False):
                        draw_poly_outline_rgba(overlay, pts, (0, 0, 0, int(160 * a01)), width=int(outline_w))
                else:
                    iw, ih = img.get_width(), img.get_height()
                    target_w = max(1, int(ws * float(overrender)))
                    target_h = max(1, int(target_w * ih / max(1, iw) * float(note_scale_y)))
                    img_id = id(img)
                    scaled = transform_cache.get_scaled(img, target_w, target_h, img_id)
                    if scaled is None:
                        scaled = pygame.transform.smoothscale(img, (target_w, target_h))
                        transform_cache.put_scaled(img, target_w, target_h, img_id, scaled)
                    angle_deg = -float(nr) * 180.0 / math.pi
                    scaled_key_id = (int(img_id), int(target_w), int(target_h))
                    rotated = transform_cache.get_rotated(scaled, angle_deg, scaled_key_id)
                    if rotated is None:
                        rotated = pygame.transform.rotate(scaled, angle_deg)
                        transform_cache.put_rotated(scaled, angle_deg, scaled_key_id, rotated)
                    try:
                        rg = rotated.copy()
                        rg.fill((255, 80, 80, 255), special_flags=pygame.BLEND_RGBA_MULT)
                        rg.set_alpha(int(200 * a01))
                        overlay.blit(rg, (ps[0] - rg.get_width() / 2, ps[1] - rg.get_height() / 2))
                    except Exception:
                        pass
                kept.append(g)
            except Exception:
                continue
        bad_ghosts[:] = kept

    base.blit(overlay, (0, 0))
    surface_pool.release(overlay)

    return (
        base,
        line_text_draw_calls,
        int(note_render_count),
        int(last_debug_ms),
        bg_scaled_cache_key,
        bg_scaled_cache,
        dim_surf_cache_key,
        dim_surf_cache,
    )
