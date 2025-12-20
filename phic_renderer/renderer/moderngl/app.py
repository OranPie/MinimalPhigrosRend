from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple
import struct

from .renderer2d import create_renderer2d
from .sprite import create_sprite_program, draw_textured_quad, draw_textured_quad_pts, draw_textured_quad_pts_uv
from .texture import load_texture_rgba, texture_from_pil_image
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...math.util import apply_expand_xy, now_sec, rect_corners, clamp
from ...core.constants import NOTE_TYPE_COLORS
from ...runtime.effects import HitFX, ParticleBurst
from ...core.fx import prune_particles
from ...runtime.judge import Judge
from ...runtime.judge import JUDGE_WEIGHT
from ...types import NoteState
from ... import state


@dataclass
class GLApp:
    ctx: Any
    window_size: tuple[int, int]
    r2d: Any
    sprite: Any
    test_tex: Any
    args: Any
    render_ctx: Dict[str, Any]
    t0: float
    _text_cache: Dict[Tuple[str, int, str], Any] = field(default_factory=dict)
    _text_cache_order: List[Tuple[str, int, str]] = field(default_factory=list)
    _hitfx: List[HitFX] = field(default_factory=list)
    _hitfx_fired: set[int] = field(default_factory=set)
    _hold_next_fx: Dict[int, float] = field(default_factory=dict)
    _particles: List[ParticleBurst] = field(default_factory=list)
    _states: List[NoteState] = field(default_factory=list)
    _idx_next: int = 0
    _judge: Any = None
    _down: bool = False
    _press_edge: bool = False
    _line_last_hit_ms: Dict[int, int] = field(default_factory=dict)
    _prev_tick_ms: int = 0
    _tick_acc_ms: float = 0.0
    _last_tick_ms: int = 0
    _fps_smooth: float = 0.0
    _note_render_count_last: int = 0

    def _set_weight(self, w: float) -> None:
        try:
            self.r2d.prog["u_weight"].value = float(w)
        except:
            pass

    def set_input(self, *, down: bool, press_edge: bool) -> None:
        self._down = bool(down)
        self._press_edge = bool(press_edge)

    def _now_tick_ms(self, dt: float) -> int:
        if self._prev_tick_ms <= 0:
            self._prev_tick_ms = 0
            self._tick_acc_ms = 0.0
        self._tick_acc_ms += float(dt) * 1000.0
        if self._tick_acc_ms < 0.0:
            self._tick_acc_ms = 0.0
        self._prev_tick_ms += int(self._tick_acc_ms)
        self._tick_acc_ms = float(self._tick_acc_ms) - float(int(self._tick_acc_ms))
        return int(self._prev_tick_ms)

    def _mark_line_hit(self, lid: int, now_tick: int) -> None:
        self._line_last_hit_ms[int(lid)] = int(now_tick)

    def _ensure_judge_state(self) -> None:
        if self._judge is None:
            self._judge = Judge()
        if not self._states:
            notes = self.render_ctx.get("notes") or []
            self._states = [NoteState(n) for n in notes]
            self._idx_next = 0

    def _get_text_texture(self, text: str, *, font_path: Optional[str], font_size: int):
        if not text:
            return None

        fp = str(font_path or "")
        key = (fp, int(font_size), str(text))
        cached = self._text_cache.get(key)
        if cached is not None:
            return cached

        try:
            from PIL import Image, ImageDraw, ImageFont  # type: ignore
        except:
            raise SystemExit(
                "ModernGL text rendering requires Pillow. Install it: pip install pillow"
            )

        use_path = None
        if fp and fp and __import__("os").path.exists(fp):
            use_path = fp
        elif __import__("os").path.exists("cmdysj.ttf"):
            use_path = "cmdysj.ttf"

        try:
            if use_path:
                font = ImageFont.truetype(use_path, int(font_size))
            else:
                font = ImageFont.load_default()
        except:
            font = ImageFont.load_default()

        parts = str(text).split("\n")
        try:
            bb = font.getbbox("Hg")
            line_h = max(1, int(bb[3] - bb[1]))
        except:
            line_h = max(1, int(font_size))

        widths: List[int] = []
        for s in parts:
            if not s:
                widths.append(0)
                continue
            try:
                b = font.getbbox(s)
                widths.append(max(1, int(b[2] - b[0])))
            except:
                widths.append(max(1, int(len(s) * max(6, int(font_size * 0.6)))))

        w = max(1, max(widths) if widths else 1)
        h = max(1, int(line_h) * max(1, len(parts)))
        img = Image.new("RGBA", (int(w), int(h)), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        y = 0
        for s in parts:
            if s:
                draw.text((0, y), s, font=font, fill=(255, 255, 255, 255))
            y += int(line_h)

        tex = texture_from_pil_image(self.ctx, img, flip_y=True)

        self._text_cache[key] = tex
        self._text_cache_order.append(key)
        if len(self._text_cache_order) > 128:
            oldk = self._text_cache_order.pop(0)
            old = self._text_cache.pop(oldk, None)
            if old is not None:
                try:
                    old.tex.release()
                except:
                    pass
        return tex

    def _update_hitfx(self, t: float, dt: float) -> None:
        self._ensure_judge_state()
        respack = self.render_ctx.get("respack", None)
        dur = 0.18
        if respack is not None:
            try:
                dur = max(1e-6, float(respack.hitfx_duration))
            except:
                dur = 0.18
        self._hitfx[:] = [fx for fx in self._hitfx if (t - float(fx.t0)) <= float(dur)]

        now_tick = self._now_tick_ms(float(dt))
        self._last_tick_ms = int(now_tick)
        respack = self.render_ctx.get("respack", None)
        hitsound = self.render_ctx.get("hitsound", None)
        lines = self.render_ctx.get("lines") or []

        try:
            self._particles[:] = prune_particles(list(self._particles), int(now_tick))
        except:
            pass

        hold_fx_interval_ms = max(10, int(getattr(self.args, "hold_fx_interval_ms", 200) or 200))
        hold_tail_tol = clamp(float(getattr(self.args, "hold_tail_tol", 0.8) or 0.8), 0.0, 1.0)

        if bool(getattr(self.args, "autoplay", False)):
            for s in self._states[max(0, self._idx_next - 20) : min(len(self._states), self._idx_next + 300)]:
                if s.judged or s.note.fake:
                    continue
                n = s.note
                if int(n.kind) != 3:
                    if abs(float(t) - float(n.t_hit)) <= float(Judge.PERFECT):
                        self._judge.bump()
                        s.judged = True
                        s.hit = True
                        try:
                            ln = lines[int(n.line_id)]
                            lx, ly, lr, _la, sc, _la_raw = eval_line_state(ln, t)
                            x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
                        except:
                            x, y, lr = 0.0, 0.0, 0.0
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            try:
                                rr, gg, bb = n.tint_hitfx_rgb
                                c = (int(rr), int(gg), int(bb), 255)
                            except:
                                pass
                        elif respack is not None:
                            c = respack.judge_colors.get("PERFECT", c)
                        self._hitfx.append(HitFX(x=x, y=y, t0=float(t), rgba=c, rot=float(lr)))
                        if respack is not None and (not bool(getattr(respack, "hide_particles", False))):
                            try:
                                self._particles.append(ParticleBurst(x, y, int(now_tick), int(float(respack.hitfx_duration) * 1000.0), c))
                            except:
                                pass
                        self._mark_line_hit(int(n.line_id), now_tick)
                        if hitsound is not None:
                            try:
                                hitsound.play(n, now_tick, respack=respack)
                            except:
                                pass
                else:
                    if (not s.holding) and abs(float(t) - float(n.t_hit)) <= float(Judge.PERFECT):
                        s.hit = True
                        s.holding = True
                        s.next_hold_fx_ms = int(now_tick + hold_fx_interval_ms)
                        s.hold_grade = "PERFECT"
                        try:
                            ln = lines[int(n.line_id)]
                            lx, ly, lr, _la, sc, _la_raw = eval_line_state(ln, t)
                            x, y = note_world_pos(lx, ly, lr, sc, n, sc, for_tail=False)
                        except:
                            x, y, lr = 0.0, 0.0, 0.0
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            try:
                                rr, gg, bb = n.tint_hitfx_rgb
                                c = (int(rr), int(gg), int(bb), 255)
                            except:
                                pass
                        elif respack is not None:
                            c = respack.judge_colors.get("PERFECT", c)
                        self._hitfx.append(HitFX(x=x, y=y, t0=float(t), rgba=c, rot=float(lr)))
                        if respack is not None and (not bool(getattr(respack, "hide_particles", False))):
                            try:
                                self._particles.append(ParticleBurst(x, y, int(now_tick), int(float(respack.hitfx_duration) * 1000.0), c))
                            except:
                                pass
                        if hitsound is not None:
                            try:
                                hitsound.play(n, now_tick, respack=respack)
                            except:
                                pass
                    if s.holding and float(t) >= float(n.t_end):
                        s.holding = False

        if self._press_edge and (not bool(getattr(self.args, "autoplay", False))):
            best: Optional[NoteState] = None
            best_dt = 1e9
            for s in self._states[max(0, self._idx_next - 50) : min(len(self._states), self._idx_next + 500)]:
                if s.judged or s.note.fake:
                    continue
                dt_hit = abs(float(t) - float(s.note.t_hit))
                if dt_hit <= float(Judge.BAD) and dt_hit < float(best_dt):
                    best = s
                    best_dt = dt_hit
            if best is not None:
                n = best.note
                if n.kind == 3:
                    grade = self._judge.grade_window(n.t_hit, t)
                    if grade is not None:
                        if grade == "BAD":
                            grade = "GOOD"

                        best.hit = True
                        best.holding = True
                        best.hold_grade = grade
                        # Hold counts into combo at press time
                        self._judge.bump()
                        best.next_hold_fx_ms = int(t * 1000.0) + hold_fx_interval_ms
                        try:
                            ln = lines[int(n.line_id)]
                            lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, t)
                            x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                        except:
                            x, y, lr = 0.0, 0.0, 0.0
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            try:
                                rr, gg, bb = n.tint_hitfx_rgb
                                c = (int(rr), int(gg), int(bb), 255)
                            except:
                                pass
                        elif respack is not None:
                            c = (
                                respack.judge_colors.get(grade)
                                or respack.judge_colors.get("GOOD")
                                or respack.judge_colors.get("PERFECT")
                                or c
                            )
                        self._hitfx.append(HitFX(x=x, y=y, t0=float(t), rgba=c, rot=float(lr)))
                        if respack is not None and (not bool(getattr(respack, "hide_particles", False))):
                            try:
                                self._particles.append(ParticleBurst(x, y, int(now_tick), int(float(respack.hitfx_duration) * 1000.0), c))
                            except:
                                pass
                        self._mark_line_hit(int(n.line_id), now_tick)
                        if hitsound is not None:
                            try:
                                hitsound.play(n, now_tick, respack=respack)
                            except:
                                pass
                else:
                    grade = self._judge.try_hit(best, float(t))
                    if grade is not None:
                        try:
                            ln = lines[int(n.line_id)]
                            lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, t)
                            x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                        except:
                            x, y, lr = 0.0, 0.0, 0.0
                        c = (255, 255, 255, 255)
                        if getattr(n, "tint_hitfx_rgb", None) is not None:
                            try:
                                rr, gg, bb = n.tint_hitfx_rgb
                                c = (int(rr), int(gg), int(bb), 255)
                            except:
                                pass
                        elif respack is not None:
                            c = (
                                respack.judge_colors.get(grade)
                                or respack.judge_colors.get("GOOD")
                                or respack.judge_colors.get("PERFECT")
                                or c
                            )
                        self._hitfx.append(HitFX(x=x, y=y, t0=float(t), rgba=c, rot=float(lr)))
                        if respack is not None and (not bool(getattr(respack, "hide_particles", False))):
                            try:
                                self._particles.append(ParticleBurst(x, y, int(now_tick), int(float(respack.hitfx_duration) * 1000.0), c))
                            except:
                                pass
                        if hitsound is not None:
                            try:
                                hitsound.play(n, now_tick, respack=respack)
                            except:
                                pass

        if not bool(getattr(self.args, "autoplay", False)):
            for s in self._states[max(0, self._idx_next - 50) : min(len(self._states), self._idx_next + 500)]:
                if s.judged or s.note.fake:
                    continue
                n = s.note
                if int(n.kind) == 3 and s.holding:
                    if (not self._down) and float(t) < float(n.t_end) - 1e-6:
                        s.released_early = True
                        s.holding = False
                    if float(t) >= float(n.t_end):
                        s.holding = False

        for s in self._states[max(0, self._idx_next - 200) : min(len(self._states), self._idx_next + 800)]:
            n = s.note
            if n.fake or int(n.kind) != 3 or s.hold_finalized:
                continue

            if (not s.hit) and (not s.hold_failed) and (float(t) > float(n.t_hit) + float(Judge.BAD)):
                s.hold_failed = True
                self._judge.break_combo()

            if s.released_early and (not s.hold_finalized):
                dur_h = max(1e-6, (float(n.t_end) - float(n.t_hit)))
                prog = clamp((float(t) - float(n.t_hit)) / dur_h, 0.0, 1.0)
                if prog < float(hold_tail_tol):
                    s.hold_failed = True
                    self._judge.break_combo()
                else:
                    g = s.hold_grade or "PERFECT"
                    self._judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    self._judge.judged_cnt += 1
                    s.hold_finalized = True

            if float(t) >= float(n.t_end) and (not s.hold_finalized):
                if s.hit and (not s.hold_failed):
                    g = s.hold_grade or "PERFECT"
                    self._judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    self._judge.judged_cnt += 1
                else:
                    self._judge.mark_miss(s)
                s.hold_finalized = True
                s.judged = True

        if respack is not None:
            for s in self._states[max(0, self._idx_next - 200) : min(len(self._states), self._idx_next + 800)]:
                n = s.note
                if n.fake or int(n.kind) != 3 or (not s.holding) or s.judged:
                    continue
                if float(t) >= float(n.t_end):
                    continue
                if int(s.next_hold_fx_ms) <= 0:
                    s.next_hold_fx_ms = int(now_tick + hold_fx_interval_ms)
                    continue
                while int(now_tick) >= int(s.next_hold_fx_ms) and float(t) < float(n.t_end):
                    try:
                        ln = lines[int(n.line_id)]
                        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, t)
                        x, y = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
                    except:
                        x, y, lr = 0.0, 0.0, 0.0
                    c = respack.judge_colors.get("PERFECT", (255, 255, 255, 255))
                    if getattr(n, "tint_hitfx_rgb", None) is not None:
                        try:
                            rr, gg, bb = n.tint_hitfx_rgb
                            c = (int(rr), int(gg), int(bb), 255)
                        except:
                            pass
                    self._hitfx.append(HitFX(x=x, y=y, t0=float(t), rgba=c, rot=float(lr)))
                    if not bool(getattr(respack, "hide_particles", False)):
                        try:
                            self._particles.append(ParticleBurst(x, y, int(now_tick), int(float(respack.hitfx_duration) * 1000.0), c))
                        except:
                            pass
                    self._mark_line_hit(int(n.line_id), now_tick)
                    s.next_hold_fx_ms = int(s.next_hold_fx_ms + hold_fx_interval_ms)

        miss_window = float(Judge.BAD)
        for s in self._states[max(0, self._idx_next - 200) : min(len(self._states), self._idx_next + 800)]:
            if s.judged or s.note.fake:
                continue
            if int(s.note.kind) == 3:
                continue
            if float(t) > float(s.note.t_hit) + miss_window:
                self._judge.mark_miss(s)

        while self._idx_next < len(self._states) and self._states[self._idx_next].judged:
            self._idx_next += 1
        try:
            self.sprite.prog["u_weight"].value = float(w)
        except:
            pass

    def _render_scene(self, t: float) -> None:
        self.r2d.begin_frame()

        W, H = self.window_size

        # Background (align with pygame: background image + dim overlay)
        bg_tex = self.render_ctx.get("bg_tex", None)
        if bg_tex is not None:
            try:
                draw_textured_quad(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=bg_tex,
                    window_size=self.window_size,
                    x0=0.0,
                    y0=0.0,
                    x1=float(W),
                    y1=float(H),
                    rgba=(255, 255, 255, 255),
                )
            except:
                pass

        try:
            dim = self.render_ctx.get("bg_dim_alpha", None)
            if dim is None:
                dim = clamp(getattr(self.args, "bg_dim", 120), 0, 255)
            dim = int(dim)
        except:
            dim = 120
        if dim > 0:
            try:
                draw_quad_pts(
                    [(0.0, 0.0), (float(W), 0.0), (float(W), float(H)), (0.0, float(H))],
                    (0, 0, 0, int(dim)),
                )
            except:
                pass

        offset = float(self.render_ctx.get("offset", 0.0) or 0.0)
        advance_active = bool(self.render_ctx.get("advance_active", False))
        use_bgm_clock = bool(self.render_ctx.get("use_bgm_clock", False))
        if (not advance_active) and use_bgm_clock:
            try:
                audio = self.render_ctx.get("audio", None)
                audio_t = float(audio.music_pos_sec() or 0.0) if audio else 0.0
                t = (audio_t - offset) * float(getattr(self.args, "chart_speed", 1.0) or 1.0)
            except:
                pass

        if (not advance_active) and (not use_bgm_clock) and getattr(self.args, "start_time", None) is not None:
            # Fallback: if no audio clock, keep legacy behavior (shift chart time only).
            t += float(self.args.start_time)

        if (not advance_active) and getattr(self.args, "end_time", None) is not None:
            try:
                if float(t) > float(self.args.end_time):
                    try:
                        audio = self.render_ctx.get("audio", None)
                        if audio:
                            audio.stop_music()
                    except:
                        pass
                    try:
                        import pygame  # type: ignore

                        pygame.event.post(pygame.event.Event(pygame.QUIT))
                    except:
                        pass
                    return
            except:
                pass

        lines = self.render_ctx.get("lines") or []
        notes = self.render_ctx.get("notes") or []
        expand = float(self.render_ctx.get("expand") or getattr(self.args, "expand", 1.0) or 1.0)
        if expand <= 1.000001:
            expand = 1.0

        line_len = float(6.75 * W)
        line_w = max(1.0, 4.0 / float(expand))

        def pack_tri(verts: List[tuple[float, float, float, float, float, float]]) -> bytes:
            return b"".join(struct.pack("6f", *v) for v in verts)

        def draw_quad_pts(pts, rgba):
            r, g, b, a = rgba
            cf = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)
            (x0, y0), (x1, y1), (x2, y2), (x3, y3) = pts
            verts = [
                (x0, y0, *cf),
                (x1, y1, *cf),
                (x2, y2, *cf),
                (x0, y0, *cf),
                (x2, y2, *cf),
                (x3, y3, *cf),
            ]
            self.r2d.draw_triangles(pack_tri(verts), 6)

        import math

        for ln in lines:
            lx, ly, lr, la01, _sc, _la_raw = eval_line_state(ln, t)
            if la01 <= 1e-6:
                continue

            if getattr(ln, "text", None) is not None:
                try:
                    s = str(ln.text.eval(t) if hasattr(ln.text, "eval") else "")
                except:
                    s = ""
                if s:
                    rr, gg, bb = (255, 255, 255)
                    try:
                        if getattr(self.args, "multicolor_lines", False) and getattr(ln, "color", None) is not None:
                            rr, gg, bb = ln.color.eval(t)
                    except:
                        rr, gg, bb = (255, 255, 255)
                    try:
                        font_mul = float(getattr(self.args, "font_size_multiplier", 1.0) or 1.0)
                    except:
                        font_mul = 1.0
                    if font_mul <= 1e-9:
                        font_mul = 1.0
                    font_size = max(1, int(round(16 * float(font_mul))))
                    tex = self._get_text_texture(s, font_path=getattr(self.args, "font_path", None), font_size=font_size)
                    if tex is not None:
                        x0, y0 = apply_expand_xy(lx, ly, W, H, expand)
                        tw, th = tex.size
                        draw_textured_quad(
                            ctx=self.ctx,
                            sp=self.sprite,
                            tex=tex,
                            window_size=self.window_size,
                            x0=float(x0),
                            y0=float(y0),
                            x1=float(x0 + tw),
                            y1=float(y0 + th),
                            rgba=(int(rr), int(gg), int(bb), int(255 * la01)),
                        )
                continue

            sx = 1.0
            sy = 1.0
            try:
                if getattr(ln, "scale_x", None) is not None:
                    sx = float(ln.scale_x.eval(t) if hasattr(ln.scale_x, "eval") else 1.0)
            except:
                sx = 1.0
            try:
                if getattr(ln, "scale_y", None) is not None:
                    sy = float(ln.scale_y.eval(t) if hasattr(ln.scale_y, "eval") else 1.0)
            except:
                sy = 1.0
            if sx <= 1e-6:
                sx = 1.0
            if sy <= 1e-6:
                sy = 1.0
            tx, ty = math.cos(lr), math.sin(lr)
            nx, ny = -ty, tx
            hl = (line_len * sx) * 0.5
            x0, y0 = lx - tx * hl, ly - ty * hl
            x1, y1 = lx + tx * hl, ly + ty * hl
            hw = (line_w * sy) * 0.5
            q = [
                (x0 + nx * hw, y0 + ny * hw),
                (x1 + nx * hw, y1 + ny * hw),
                (x1 - nx * hw, y1 - ny * hw),
                (x0 - nx * hw, y0 - ny * hw),
            ]
            q = [apply_expand_xy(px, py, W, H, expand) for (px, py) in q]
            if getattr(self.args, "multicolor_lines", False):
                if getattr(ln, "color", None) is not None:
                    try:
                        rr, gg, bb = ln.color.eval(t)
                    except:
                        rr, gg, bb = ln.color_rgb
                else:
                    rr, gg, bb = ln.color_rgb
            else:
                rr, gg, bb = (255, 255, 255)
            draw_quad_pts(q, (int(rr), int(gg), int(bb), int(255 * la01)))

        respack = self.render_ctx.get("respack", None)

        def pick_note_tex(note: Any):
            if not respack:
                return None
            try:
                mh = bool(getattr(note, "mh", False))
                if int(note.kind) == 1:
                    return respack.img["click_mh.png"] if mh else respack.img["click.png"]
                if int(note.kind) == 2:
                    return respack.img["drag_mh.png"] if mh else respack.img["drag.png"]
                if int(note.kind) == 3:
                    return respack.img["hold_mh.png"] if mh else respack.img["hold.png"]
                if int(note.kind) == 4:
                    return respack.img["flick_mh.png"] if mh else respack.img["flick.png"]
            except:
                return None
            return None

        base_note_w = float(0.06 * W)
        base_note_h = float(0.018 * H)
        _note_scale_x_raw = float(getattr(self.args, "note_scale_x", 1.0) or 1.0)
        _note_scale_y_raw = float(getattr(self.args, "note_scale_y", 1.0) or 1.0)
        _ex = float(expand) if expand is not None else 1.0
        if _ex <= 1.000001:
            _ex = 1.0
        note_scale_x = _note_scale_x_raw / _ex
        note_scale_y = _note_scale_y_raw / _ex
        approach = float(getattr(self.args, "approach", 3.0) or 3.0)

        hold_body_w = float(0.035 * W * note_scale_x)
        outline_w = max(1.0, 2.0 / float(expand))

        def draw_hold_3slice(
            *,
            head_xy: tuple[float, float],
            tail_xy: tuple[float, float],
            line_rot: float,
            alpha01: float,
            line_rgb: tuple[int, int, int],
            note_rgb: tuple[int, int, int],
            size_scale: float,
            mh: bool,
            progress: Optional[float] = None,
            draw_outline: bool,
            outline_width: float,
        ) -> None:
            if respack is None:
                return
            try:
                tex = respack.img["hold_mh.png"] if mh else respack.img["hold.png"]
            except:
                return
            if tex is None:
                return

            vx = float(tail_xy[0]) - float(head_xy[0])
            vy = float(tail_xy[1]) - float(head_xy[1])
            length = math.hypot(vx, vy)
            if length <= 1e-3:
                return
            ux, uy = vx / length, vy / length
            nx, ny = -uy, ux

            iw, ih = tex.size
            try:
                tail_h = int(respack.hold_tail_h_mh) if mh else int(respack.hold_tail_h)
                head_h = int(respack.hold_head_h_mh) if mh else int(respack.hold_head_h)
            except:
                tail_h, head_h = 50, 50
            tail_h = max(0, min(int(ih), int(tail_h)))
            head_h = max(0, min(int(ih), int(head_h)))
            mid_h = max(1, int(ih) - int(tail_h) - int(head_h))

            target_w = max(2.0, float(hold_body_w) * float(size_scale))
            scale = float(target_w) / max(1.0, float(iw))

            tail_len = float(tail_h) * scale
            head_len = float(head_h) * scale

            if bool(getattr(respack, "hold_compact", False)):
                mid_len = float(length)
            else:
                if (tail_len + head_len) > float(length):
                    s = float(length) / max(1e-6, (tail_len + head_len))
                    tail_len *= s
                    head_len *= s
                    mid_len = 1.0
                else:
                    mid_len = max(1.0, float(length) - tail_len - head_len)

            u0, u1 = 0.0, 1.0
            denom = float(max(1, int(ih)))
            v_tail0 = 0.0
            v_tail1 = float(tail_h) / denom
            v_head0 = 1.0 - (float(head_h) / denom)
            v_head1 = 1.0
            v_mid0 = float(v_tail1)
            v_mid1 = float(v_head0)
            if float(v_mid1) < float(v_mid0):
                v_mid1 = float(v_mid0)

            hw = float(target_w) * 0.5
            r, g, b = (int(note_rgb[0]), int(note_rgb[1]), int(note_rgb[2]))
            a = int(255 * clamp(float(alpha01), 0.0, 1.0))
            if a <= 0:
                return
            rgba = (r, g, b, int(a))

            def _quad(a0: float, a1: float) -> list[tuple[float, float]]:
                sx = float(head_xy[0]) + ux * float(a0)
                sy = float(head_xy[1]) + uy * float(a0)
                ex = float(head_xy[0]) + ux * float(a1)
                ey = float(head_xy[1]) + uy * float(a1)
                return [
                    (sx + nx * hw, sy + ny * hw),
                    (sx - nx * hw, sy - ny * hw),
                    (ex - nx * hw, ey - ny * hw),
                    (ex + nx * hw, ey + ny * hw),
                ]

            # Post-hit visual crop: sample only the "tail side" portion of the texture and stretch it
            # across the current geometric length.
            if progress is not None:
                try:
                    p = clamp(float(progress), 0.0, 1.0)
                except:
                    p = None
                if p is not None and p > 1e-6:
                    keep = clamp(1.0 - float(p), 0.02, 1.0)
                    try:
                        pts = _quad(0.0, float(length))
                        draw_textured_quad_pts_uv(
                            ctx=self.ctx,
                            sp=self.sprite,
                            tex=tex,
                            window_size=self.window_size,
                            pts=pts,
                            uv0=(u0, 0.0),
                            uv1=(u1, float(keep)),
                            rgba=rgba,
                        )
                        return
                    except:
                        pass

            if draw_outline and outline_width > 0.0:
                try:
                    ow = max(0.5, float(outline_width))
                    hw_in = max(0.0, hw - ow)
                    sx = float(head_xy[0])
                    sy = float(head_xy[1])
                    ex = float(tail_xy[0])
                    ey = float(tail_xy[1])
                    p0 = (sx + nx * hw, sy + ny * hw)
                    p1 = (sx - nx * hw, sy - ny * hw)
                    p2 = (ex - nx * hw, ey - ny * hw)
                    p3 = (ex + nx * hw, ey + ny * hw)
                    q0 = (sx + nx * hw_in, sy + ny * hw_in)
                    q1 = (sx - nx * hw_in, sy - ny * hw_in)
                    q2 = (ex - nx * hw_in, ey - ny * hw_in)
                    q3 = (ex + nx * hw_in, ey + ny * hw_in)
                    lr, lg, lb = (int(line_rgb[0]), int(line_rgb[1]), int(line_rgb[2]))
                    rgba_o = (lr, lg, lb, int(a))
                    draw_quad_pts([p0, q0, q3, p3], rgba_o)
                    draw_quad_pts([q1, p1, p2, q2], rgba_o)
                    draw_quad_pts([p0, p1, q1, q0], rgba_o)
                    draw_quad_pts([q3, q2, p2, p3], rgba_o)
                except:
                    pass

            def _draw_mid_repeat(a_start: float, seg_len: float) -> None:
                tile_len = float(mid_h) * scale
                if tile_len <= 1e-6:
                    return
                if not bool(getattr(respack, "hold_repeat", False)):
                    pts = _quad(a_start, a_start + float(seg_len))
                    draw_textured_quad_pts_uv(
                        ctx=self.ctx,
                        sp=self.sprite,
                        tex=tex,
                        window_size=self.window_size,
                        pts=pts,
                        uv0=(u0, float(v_mid0)),
                        uv1=(u1, float(v_mid1)),
                        rgba=rgba,
                    )
                    return

                pos = float(a_start)
                rem = float(seg_len)
                while rem > 1e-6:
                    cur = min(float(tile_len), float(rem))
                    v_end = float(v_mid0) + (float(v_mid1) - float(v_mid0)) * clamp(cur / max(1e-6, tile_len), 0.0, 1.0)
                    pts = _quad(pos, pos + cur)
                    draw_textured_quad_pts_uv(
                        ctx=self.ctx,
                        sp=self.sprite,
                        tex=tex,
                        window_size=self.window_size,
                        pts=pts,
                        uv0=(u0, float(v_mid0)),
                        uv1=(u1, float(v_end)),
                        rgba=rgba,
                    )
                    pos += cur
                    rem -= cur

            if bool(getattr(respack, "hold_compact", False)):
                _draw_mid_repeat(0.0, float(length))
                if head_len > 1e-6:
                    pts = _quad(0.0, float(head_len))
                    draw_textured_quad_pts_uv(
                        ctx=self.ctx,
                        sp=self.sprite,
                        tex=tex,
                        window_size=self.window_size,
                        pts=pts,
                        uv0=(u0, float(v_head0)),
                        uv1=(u1, float(v_head1)),
                        rgba=rgba,
                    )
                if tail_len > 1e-6:
                    pts = _quad(max(0.0, float(length) - float(tail_len)), float(length))
                    draw_textured_quad_pts_uv(
                        ctx=self.ctx,
                        sp=self.sprite,
                        tex=tex,
                        window_size=self.window_size,
                        pts=pts,
                        uv0=(u0, float(v_tail0)),
                        uv1=(u1, float(v_tail1)),
                        rgba=rgba,
                    )
                return

            if head_len > 1e-6:
                pts = _quad(0.0, float(head_len))
                draw_textured_quad_pts_uv(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    uv0=(u0, float(v_head0)),
                    uv1=(u1, float(v_head1)),
                    rgba=rgba,
                )

            _draw_mid_repeat(float(head_len), float(mid_len))

            if tail_len > 1e-6:
                pts = _quad(float(head_len + mid_len), float(head_len + mid_len + tail_len))
                draw_textured_quad_pts_uv(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    uv0=(u0, float(v_tail0)),
                    uv1=(u1, float(v_tail1)),
                    rgba=rgba,
                )

        self._ensure_judge_state()
        no_cull_all = bool(getattr(self.args, "no_cull", False))
        no_cull_screen = bool(getattr(self.args, "no_cull_screen", False))
        no_cull_enter_time = bool(getattr(self.args, "no_cull_enter_time", False))
        note_render_count = 0
        for s in (self._states or []):
            n = s.note
            if int(n.kind) != 3 and bool(getattr(s, "judged", False)):
                continue
            if getattr(n, "fake", False):
                continue
            if (not no_cull_all) and (not no_cull_enter_time):
                t_enter = float(getattr(n, "t_enter", -1e9))
                if t < t_enter:
                    continue
                if t > float(n.t_hit) + max(0.25, approach + 0.5):
                    continue

            ln = lines[int(n.line_id)]
            lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)

            note_alpha = clamp(float(getattr(n, "alpha01", 1.0) or 1.0), 0.0, 1.0)
            try:
                mode = str(getattr(self.args, "line_alpha_affects_notes", "negative_only"))
            except:
                mode = "negative_only"
            if float(la01) < 0.0:
                if mode != "never":
                    note_alpha *= clamp(1.0 + float(la01), 0.0, 1.0)
            elif mode == "always":
                note_alpha *= clamp(float(la01), 0.0, 1.0)
            if note_alpha <= 1e-6:
                continue

            if int(n.kind) == 3:
                hit_for_draw = bool(getattr(s, "hit", False)) and (not bool(getattr(n, "fake", False)))
                if hit_for_draw:
                    head = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
                else:
                    head_target_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
                    head = note_world_pos(lx, ly, lr, sc_now, n, head_target_scroll, for_tail=False)
                tail = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_end, for_tail=True)
                head_s = apply_expand_xy(float(head[0]), float(head[1]), W, H, expand)
                tail_s = apply_expand_xy(float(tail[0]), float(tail[1]), W, H, expand)

                if (not no_cull_all) and (not no_cull_screen):
                    try:
                        m = 140.0
                        minx = min(float(head_s[0]), float(tail_s[0]))
                        maxx = max(float(head_s[0]), float(tail_s[0]))
                        miny = min(float(head_s[1]), float(tail_s[1]))
                        maxy = max(float(head_s[1]), float(tail_s[1]))
                        if maxx < -m or minx > float(W) + m or maxy < -m or miny > float(H) + m:
                            continue
                    except:
                        pass
                hold_alpha = float(note_alpha)
                if bool(getattr(s, "hold_failed", False)):
                    hold_alpha *= 0.35
                mh = bool(getattr(n, "mh", False))
                size_scale = float(getattr(n, "size_px", 1.0) or 1.0)
                note_rgb = getattr(n, "tint_rgb", (255, 255, 255))
                line_rgb = ln.color_rgb

                prog = None
                try:
                    if bool(getattr(s, "hit", False)) or bool(getattr(s, "holding", False)):
                        if float(sc_now) > float(n.scroll_hit) + 1e-6 and (float(n.scroll_end) > float(n.scroll_hit) + 1e-6):
                            den = float(n.scroll_end) - float(n.scroll_hit)
                            prog = clamp((float(sc_now) - float(n.scroll_hit)) / max(1e-6, den), 0.0, 1.0)
                except:
                    prog = None
                draw_hold_3slice(
                    head_xy=(float(head_s[0]), float(head_s[1])),
                    tail_xy=(float(tail_s[0]), float(tail_s[1])),
                    line_rot=float(lr),
                    alpha01=float(hold_alpha),
                    line_rgb=(int(line_rgb[0]), int(line_rgb[1]), int(line_rgb[2])),
                    note_rgb=(int(note_rgb[0]), int(note_rgb[1]), int(note_rgb[2])),
                    size_scale=float(size_scale),
                    mh=bool(mh),
                    progress=prog,
                    draw_outline=(not bool(getattr(self.args, "no_note_outline", True))),
                    outline_width=float(outline_w),
                )
                note_render_count += 1
                continue

            xw, yw = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
            xw, yw = apply_expand_xy(float(xw), float(yw), W, H, expand)

            if (not no_cull_all) and (not no_cull_screen):
                try:
                    m = 140.0
                    if float(xw) < -m or float(xw) > float(W) + m or float(yw) < -m or float(yw) > float(H) + m:
                        continue
                except:
                    pass

            w = base_note_w * note_scale_x * float(getattr(n, "size_px", 1.0) or 1.0)
            h = base_note_h * note_scale_y * float(getattr(n, "size_px", 1.0) or 1.0)
            pts = rect_corners(float(xw), float(yw), float(w), float(h), float(lr))

            a_note = int(255 * float(note_alpha))
            tr, tg, tb = (255, 255, 255)
            try:
                tr, tg, tb = getattr(n, "tint_rgb", (255, 255, 255))
            except:
                tr, tg, tb = (255, 255, 255)
            tex = pick_note_tex(n)
            if tex is not None:
                draw_textured_quad_pts(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    rgba=(int(tr), int(tg), int(tb), int(a_note)),
                )
            else:
                r0, g0, b0 = NOTE_TYPE_COLORS.get(int(n.kind), (255, 255, 255))
                draw_quad_pts(pts, (int(r0 * tr / 255), int(g0 * tg / 255), int(b0 * tb / 255), int(a_note)))
            note_render_count += 1

        self._note_render_count_last = int(note_render_count)

        respack = self.render_ctx.get("respack", None)
        for fx in (self._hitfx or []):
            age = float(t) - float(fx.t0)
            if respack is None:
                if age < 0.0 or age > 0.18:
                    continue
                r = float(10.0 + 140.0 * age)
                a = int(255 * (1.0 - age / 0.18))
                rr, gg, bb, _aa = fx.rgba
                x0, y0 = apply_expand_xy(float(fx.x), float(fx.y), W, H, expand)
                pts = rect_corners(float(x0), float(y0), float(r), float(r), 0.0)
                draw_quad_pts(pts, (int(rr), int(gg), int(bb), int(a)))
                continue

            dur = 0.18
            try:
                dur = max(1e-6, float(respack.hitfx_duration))
            except:
                dur = 0.18
            if age < 0.0 or age > float(dur):
                continue

            fw, fh = respack.hitfx_frames_xy
            sheet = respack.hitfx_sheet
            sw, sh = sheet.size
            cell_w = float(sw) / max(1, int(fw))
            cell_h = float(sh) / max(1, int(fh))

            p = clamp(age / float(dur), 0.0, 0.999999)
            idx = int(p * float(int(fw) * int(fh)))
            ix = int(idx % int(fw))
            iy = int(idx // int(fw))

            try:
                sc = (float(respack.hitfx_scale) * float(getattr(self.args, "hitfx_scale_mul", 1.0) or 1.0)) / float(expand)
            except:
                sc = 1.0 / float(expand)
            if sc <= 1e-6:
                sc = 1.0 / float(expand)

            x0, y0 = apply_expand_xy(float(fx.x), float(fx.y), W, H, expand)
            ang = float(fx.rot) if bool(getattr(respack, "hitfx_rotate", False)) else 0.0
            pts = rect_corners(float(x0), float(y0), float(cell_w * sc), float(cell_h * sc), float(ang))

            u0 = float(ix) / float(max(1, int(fw)))
            v0 = float(iy) / float(max(1, int(fh)))
            u1 = float(ix + 1) / float(max(1, int(fw)))
            v1 = float(iy + 1) / float(max(1, int(fh)))

            r, g, b, a = fx.rgba
            if (not bool(getattr(respack, "hitfx_tinted", True))) and (int(r), int(g), int(b)) == (255, 255, 255):
                r, g, b = (255, 255, 255)
            draw_textured_quad_pts_uv(
                ctx=self.ctx,
                sp=self.sprite,
                tex=sheet,
                window_size=self.window_size,
                pts=pts,
                uv0=(u0, v0),
                uv1=(u1, v1),
                rgba=(int(r), int(g), int(b), int(a)),
            )

        if self.test_tex is not None:
            tw, th = self.test_tex.size
            x0 = (W - tw) * 0.5
            y0 = 140
            draw_textured_quad(
                ctx=self.ctx,
                sp=self.sprite,
                tex=self.test_tex,
                window_size=self.window_size,
                x0=x0,
                y0=y0,
                x1=x0 + tw,
                y1=y0 + th,
                rgba=(255, 255, 255, 255),
            )

    def render(self, dt: float) -> None:
        offset = float(self.render_ctx.get("offset", 0.0) or 0.0)
        chart_speed = float(getattr(self.args, "chart_speed", 1.0) or 1.0)
        t_base = ((now_sec() - self.t0) - offset) * chart_speed

        self._update_hitfx(t_base, float(dt))

        samples = 1
        shutter = 0.0
        if getattr(state, "motion_blur_samples", None) is not None:
            try:
                samples = max(1, int(getattr(state, "motion_blur_samples")))
            except:
                samples = 1
        if getattr(state, "motion_blur_shutter", None) is not None:
            try:
                shutter = clamp(float(getattr(state, "motion_blur_shutter")), 0.0, 2.0)
            except:
                shutter = 0.0

        self.ctx.clear(0.06, 0.06, 0.08, 1.0)

        if getattr(self.args, "basic_debug", False):
            try:
                fps_now = 1.0 / max(1e-9, float(dt))
            except:
                fps_now = 0.0
            if self._fps_smooth <= 1e-6:
                self._fps_smooth = float(fps_now)
            else:
                self._fps_smooth = float(self._fps_smooth) * 0.9 + float(fps_now) * 0.1

        if samples <= 1 or shutter <= 1e-6:
            self._set_weight(1.0)
            self._render_scene(t_base)
            self._draw_particles()
            if getattr(self.args, "basic_debug", False):
                self._draw_basic_debug()
            self._draw_debug_overlays(t_base)
            return

        try:
            import moderngl  # type: ignore

            self.ctx.enable(moderngl.BLEND)
            old = getattr(self.ctx, "blend_func", None)
            self.ctx.blend_func = (moderngl.ONE, moderngl.ONE)
        except:
            old = None

        w = 1.0 / float(samples)
        dt_chart = float(dt) * chart_speed
        for i in range(samples):
            frac = 0.0 if samples <= 1 else (float(i) / float(samples - 1))
            t_s = t_base - shutter * dt_chart * (1.0 - frac)
            self._set_weight(w)
            self._render_scene(t_s)

        self._draw_particles()
        if getattr(self.args, "basic_debug", False):
            self._draw_basic_debug()
        self._draw_debug_overlays(t_base)

        if old is not None:
            try:
                self.ctx.blend_func = old
            except:
                pass


    def _draw_basic_debug(self) -> None:
        W, H = self.window_size
        try:
            font_mul = float(getattr(self.args, "font_size_multiplier", 1.0) or 1.0)
        except:
            font_mul = 1.0
        if font_mul <= 1e-9:
            font_mul = 1.0
        font_size = max(1, int(round(14 * float(font_mul))))
        text = f"FPS {self._fps_smooth:6.1f}  NOTE_RENDER {int(self._note_render_count_last)}"
        tex = self._get_text_texture(text, font_path=getattr(self.args, "font_path", None), font_size=font_size)
        if tex is None:
            return
        tw, th = tex.size
        draw_textured_quad(
            ctx=self.ctx,
            sp=self.sprite,
            tex=tex,
            window_size=self.window_size,
            x0=16.0,
            y0=float(H - 16 - th),
            x1=16.0 + float(tw),
            y1=float(H - 16),
            rgba=(230, 230, 230, 230),
        )

    def _draw_particles(self) -> None:
        respack = self.render_ctx.get("respack", None)
        if respack is not None and bool(getattr(respack, "hide_particles", False)):
            return
        if not self._particles:
            return

        W, H = self.window_size
        expand = float(self.render_ctx.get("expand") or getattr(self.args, "expand", 1.0) or 1.0)
        if expand <= 1.000001:
            expand = 1.0

        now_ms = int(getattr(self, "_last_tick_ms", 0) or 0)
        if now_ms <= 0:
            return

        try:
            import moderngl  # type: ignore

            self.ctx.enable(moderngl.BLEND)
            old = getattr(self.ctx, "blend_func", None)
            self.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE)
        except:
            old = None

        for p in (self._particles or []):
            try:
                parts = p.get_particles(now_ms)
            except:
                continue
            for q in parts:
                try:
                    xq, yq = apply_expand_xy(float(q["x"]), float(q["y"]), W, H, expand)
                    sz = max(1.0, float(q["size"]) / float(expand))
                    c = q["color"]
                    pts = rect_corners(float(xq), float(yq), float(sz), float(sz), 0.0)
                    draw_quad_pts(pts, (int(c[0]), int(c[1]), int(c[2]), int(c[3])))
                except:
                    pass

        if old is not None:
            try:
                self.ctx.blend_func = old
            except:
                pass

    def _draw_debug_overlays(self, t: float) -> None:
        if not (bool(getattr(self.args, "debug_note_info", False)) or bool(getattr(self.args, "debug_judge_windows", False))):
            return
        self._ensure_judge_state()

        W, H = self.window_size
        respack = self.render_ctx.get("respack", None)

        if bool(getattr(self.args, "debug_judge_windows", False)):
            try:
                p = float(Judge.PERFECT)
                g = float(Judge.GOOD)
                b = float(Judge.BAD)
                cx = float(W) * 0.5
                y = float(H) - 52.0
                total_w = 520.0
                px_per_sec = total_w / max(1e-6, (2.0 * b))
                w_bad = 2.0 * b * px_per_sec
                w_good = 2.0 * g * px_per_sec
                w_perf = 2.0 * p * px_per_sec

                col_p = (90, 220, 255, 200)
                col_g = (170, 255, 170, 180)
                col_b = (255, 200, 120, 160)
                if respack is not None:
                    try:
                        col_p = respack.judge_colors.get("PERFECT", col_p)
                        col_g = respack.judge_colors.get("GOOD", col_g)
                        col_b = respack.judge_colors.get("BAD", col_b)
                    except:
                        pass

                pts_b = rect_corners(cx, y, w_bad, 10.0, 0.0)
                pts_g = rect_corners(cx, y, w_good, 10.0, 0.0)
                pts_p = rect_corners(cx, y, w_perf, 10.0, 0.0)
                draw_quad_pts(pts_b, (int(col_b[0]), int(col_b[1]), int(col_b[2]), int(col_b[3])))
                draw_quad_pts(pts_g, (int(col_g[0]), int(col_g[1]), int(col_g[2]), int(col_g[3])))
                draw_quad_pts(pts_p, (int(col_p[0]), int(col_p[1]), int(col_p[2]), int(col_p[3])))
            except:
                pass

        if bool(getattr(self.args, "debug_note_info", False)):
            try:
                lines = self.render_ctx.get("lines") or []
                expand = float(getattr(self.args, "expand", 1.0) or 1.0)
                if expand <= 1.000001:
                    expand = 1.0
                ex = max(1.0, float(expand))
                note_scale_y = float(getattr(self.args, "note_scale_y", 1.0) or 1.0) / ex
                base_note_h = float(0.018 * float(H))
                font_size = max(1, int(round(12 * float(getattr(self.args, "font_size_multiplier", 1.0) or 1.0))))

                # Limit to avoid creating too many text textures.
                candidates = (self._states or [])[max(0, self._idx_next - 60) : min(len(self._states or []), self._idx_next + 120)]
                drawn = 0
                for s in candidates:
                    n = s.note
                    if getattr(n, "fake", False):
                        continue
                    if int(n.kind) != 3 and bool(getattr(s, "judged", False)):
                        continue
                    if int(drawn) >= 80:
                        break
                    try:
                        ln = lines[int(n.line_id)]
                    except:
                        continue

                    try:
                        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
                    except:
                        continue

                    # Compute a stable anchor point.
                    if int(n.kind) == 3:
                        try:
                            head_target_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
                            x0, y0 = note_world_pos(lx, ly, lr, sc_now, n, head_target_scroll, for_tail=False)
                        except:
                            continue
                    else:
                        try:
                            x0, y0 = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                        except:
                            continue

                    x0, y0 = apply_expand_xy(float(x0), float(y0), W, H, expand)

                    side_ch = "A" if bool(getattr(n, "above", True)) else "B"
                    label = f"{int(n.nid)}:{int(n.kind)} L{int(n.line_id)}{side_ch}"
                    tex = self._get_text_texture(label, font_path=getattr(self.args, "font_path", None), font_size=font_size)
                    if tex is None:
                        continue
                    tw, th = tex.size

                    nx = -math.sin(float(lr))
                    ny = math.cos(float(lr))
                    side = 1.0 if bool(getattr(n, "above", True)) else -1.0
                    off = (base_note_h * float(note_scale_y) * 0.9 + 10.0)
                    px = float(x0) + nx * off * side
                    py = float(y0) + ny * off * side

                    draw_textured_quad(
                        ctx=self.ctx,
                        sp=self.sprite,
                        tex=tex,
                        window_size=self.window_size,
                        x0=float(px) - float(tw) * 0.5,
                        y0=float(py) - float(th) * 0.5,
                        x1=float(px) + float(tw) * 0.5,
                        y1=float(py) + float(th) * 0.5,
                        rgba=(230, 230, 230, 220),
                    )
                    drawn += 1

                # Detailed HUD for the nearest note (dynamic text, but only one texture per frame).
                best = None
                best_dt = 1e9
                for s in (self._states or [])[max(0, self._idx_next - 60) : min(len(self._states or []), self._idx_next + 200)]:
                    try:
                        if getattr(s.note, "fake", False):
                            continue
                        dt = abs(float(t) - float(s.note.t_hit))
                        if dt < best_dt:
                            best = s
                            best_dt = dt
                    except:
                        continue
                if best is not None:
                    n = best.note
                    try:
                        ln = lines[int(n.line_id)]
                        _lx, _ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
                        dy = float(n.scroll_hit) - float(sc_now)
                    except:
                        lr = 0.0
                        sc_now = 0.0
                        dy = 0.0
                    side_ch = "A" if bool(getattr(n, "above", True)) else "B"
                    text = (
                        f"nid={int(n.nid)} kind={int(n.kind)} line={int(n.line_id)}{side_ch}\n"
                        f"t={float(t):.3f} hit={float(n.t_hit):.3f} dt={float(t - n.t_hit)*1000.0:+.1f}ms\n"
                        f"sc_now={float(sc_now):.2f} sc_hit={float(n.scroll_hit):.2f} dy={float(dy):.2f}\n"
                        f"holding={bool(getattr(best,'holding',False))} judged={bool(getattr(best,'judged',False))} lr={float(lr):+.2f}"
                    )
                    tex = self._get_text_texture(text, font_path=getattr(self.args, "font_path", None), font_size=max(1, int(round(13 * float(getattr(self.args, "font_size_multiplier", 1.0) or 1.0)))))
                    if tex is not None:
                        tw, th = tex.size
                        draw_textured_quad(
                            ctx=self.ctx,
                            sp=self.sprite,
                            tex=tex,
                            window_size=self.window_size,
                            x0=16.0,
                            y0=16.0,
                            x1=16.0 + float(tw),
                            y1=16.0 + float(th),
                            rgba=(230, 230, 230, 220),
                        )
            except:
                pass


def create_app(ctx: Any, *, window_size: tuple[int, int], args: Any, render_ctx: Dict[str, Any]) -> GLApp:
    r2d = create_renderer2d(ctx, size=window_size)
    sprite = create_sprite_program(ctx)
    tex = None
    try:
        import moderngl  # type: ignore

        ctx.enable(moderngl.BLEND)
        ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA)
    except:
        pass

    try:
        import os

        # Use one of the bundled chart jacket images as a smoke test.
        candidates = [
            os.path.join("charts", "ATHAZA.LeaF", "ATHAZA.LeaF.png"),
            os.path.join("charts", "Aleph0.LeaF", "Aleph0.LeaF.png"),
            os.path.join("charts", "Radiance.Nhato", "Radiance.Nhato.png"),
        ]
        for p in candidates:
            if os.path.exists(p):
                tex = load_texture_rgba(ctx, p, flip_y=True)
                break
    except:
        pass
    return GLApp(ctx=ctx, window_size=window_size, r2d=r2d, sprite=sprite, test_tex=tex, args=args, render_ctx=render_ctx, t0=now_sec())
