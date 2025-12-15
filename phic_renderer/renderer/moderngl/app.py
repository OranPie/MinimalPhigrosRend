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
from ...runtime.effects import HitFX
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
    _states: List[NoteState] = field(default_factory=list)
    _idx_next: int = 0
    _judge: Any = None
    _down: bool = False
    _press_edge: bool = False
    _line_last_hit_ms: Dict[int, int] = field(default_factory=dict)
    _prev_tick_ms: int = 0
    _tick_acc_ms: float = 0.0
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
        respack = self.render_ctx.get("respack", None)
        hitsound = self.render_ctx.get("hitsound", None)
        lines = self.render_ctx.get("lines") or []

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
                if int(n.kind) == 3:
                    grade = self._judge.grade_window(float(n.t_hit), float(t))
                    if grade is not None:
                        best.hit = True
                        best.holding = True
                        best.hold_grade = grade
                        best.next_hold_fx_ms = int(now_tick + hold_fx_interval_ms)
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
                    self._judge.bump()
                    s.hold_finalized = True

            if float(t) >= float(n.t_end) and (not s.hold_finalized):
                if s.hit and (not s.hold_failed):
                    g = s.hold_grade or "PERFECT"
                    self._judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                    self._judge.judged_cnt += 1
                    self._judge.bump()
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

        no_cull = bool(getattr(self.args, "no_cull", False))
        note_render_count = 0
        for n in notes:
            if getattr(n, "fake", False):
                continue
            if (not no_cull):
                t_enter = float(getattr(n, "t_enter", -1e9))
                if t < t_enter:
                    continue
                if t > float(n.t_hit) + max(0.25, approach + 0.5):
                    continue

            note_render_count += 1

            ln = lines[int(n.line_id)]
            lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
            xw, yw = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
            xw, yw = apply_expand_xy(xw, yw, W, H, expand)

            w = base_note_w * note_scale_x * float(getattr(n, "size_px", 1.0) or 1.0)
            h = base_note_h * note_scale_y * float(getattr(n, "size_px", 1.0) or 1.0)
            pts = rect_corners(xw, yw, w, h, lr)

            a_note = int(255 * clamp(float(getattr(n, "alpha01", 1.0) or 1.0), 0.0, 1.0))
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
                    rgba=(int(tr), int(tg), int(tb), a_note),
                )
            else:
                r0, g0, b0 = NOTE_TYPE_COLORS.get(int(n.kind), (255, 255, 255))
                draw_quad_pts(pts, (int(r0 * tr / 255), int(g0 * tg / 255), int(b0 * tb / 255), a_note))

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
            if getattr(self.args, "basic_debug", False):
                self._draw_basic_debug()
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

        if getattr(self.args, "basic_debug", False):
            self._draw_basic_debug()

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
