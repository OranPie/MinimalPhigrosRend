"""
Note rendering for ModernGL backend.

Renders tap, drag, hold, and flick notes with culling, alpha blending,
and tinting support.
"""

from __future__ import annotations

from typing import Any, Callable, Optional

from ....engine.kinematics import eval_line_state, note_world_pos
from ....math.util import apply_expand_xy, rect_corners, clamp
from ....core.constants import NOTE_TYPE_COLORS


class NoteRenderer:
    """
    Renders all note types (tap, drag, hold, flick).

    Handles texture selection, position calculation, culling,
    alpha blending, and color tinting.
    """

    def __init__(self, ctx: Any, window_size: tuple, hold_renderer: Any):
        """
        Initialize note renderer.

        Args:
            ctx: ModernGL context
            window_size: (width, height) tuple
            hold_renderer: HoldRenderer for hold notes
        """
        self.ctx = ctx
        self.window_size = window_size
        self.hold_renderer = hold_renderer

    def render_notes(
        self,
        states: list,
        lines: list,
        t: float,
        expand: float,
        respack: Any,
        draw_quad_fn: Callable,
        draw_textured_quad_pts_fn: Callable,
        args: Any = None,
    ) -> int:
        """
        Render all visible notes.

        Args:
            states: List of NoteState objects
            lines: List of RuntimeLine objects
            t: Current time in seconds
            expand: Expand factor for coordinates
            respack: Resource pack with note textures
            draw_quad_fn: Function to draw colored quads
            draw_textured_quad_pts_fn: Function to draw textured quads with corners
            args: Additional rendering arguments

        Returns:
            Number of notes rendered
        """
        W, H = self.window_size

        # Calculate note sizing
        base_note_w = float(0.06 * W)
        base_note_h = float(0.018 * H)

        _note_scale_x_raw = float(getattr(args, "note_scale_x", 1.0) or 1.0) if args else 1.0
        _note_scale_y_raw = float(getattr(args, "note_scale_y", 1.0) or 1.0) if args else 1.0

        _ex = float(expand) if expand is not None else 1.0
        if _ex <= 1.000001:
            _ex = 1.0

        note_scale_x = _note_scale_x_raw / _ex
        note_scale_y = _note_scale_y_raw / _ex
        approach = float(getattr(args, "approach", 3.0) or 3.0) if args else 3.0

        hold_body_w = float(0.035 * W * note_scale_x)
        outline_w = max(1.0, 2.0 / float(expand))
        draw_outline = bool(getattr(args, "note_outline", False)) if args else False
        if args and bool(getattr(args, "no_note_outline", False)):
            draw_outline = False

        # Culling options
        no_cull_all = bool(getattr(args, "no_cull", False)) if args else False
        no_cull_screen = bool(getattr(args, "no_cull_screen", False)) if args else False
        no_cull_enter_time = bool(getattr(args, "no_cull_enter_time", False)) if args else False

        note_render_count = 0

        for s in (states or []):
            n = s.note

            # Skip judged non-hold notes
            if int(n.kind) != 3 and bool(getattr(s, "judged", False)):
                continue

            # Skip fake notes
            if getattr(n, "fake", False):
                continue

            # Time-based culling
            if (not no_cull_all) and (not no_cull_enter_time):
                t_enter = float(getattr(n, "t_enter", -1e9))
                if t < t_enter:
                    continue
                if t > float(n.t_hit) + max(0.25, approach + 0.5):
                    continue

            # Get line state
            ln = lines[int(n.line_id)]
            lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)

            # Calculate note alpha
            note_alpha = clamp(float(getattr(n, "alpha01", 1.0) or 1.0), 0.0, 1.0)

            try:
                mode = str(getattr(args, "line_alpha_affects_notes", "negative_only")) if args else "negative_only"
            except:
                mode = "negative_only"

            if float(la01) < 0.0:
                if mode != "never":
                    note_alpha *= clamp(1.0 + float(la01), 0.0, 1.0)
            elif mode == "always":
                note_alpha *= clamp(float(la01), 0.0, 1.0)

            if note_alpha <= 1e-6:
                continue

            # Stop rendering completed notes
            if int(n.kind) == 3:
                if bool(getattr(s, "hold_finalized", False)):
                    continue
                try:
                    if float(t) >= float(getattr(n, "t_end", 0.0)) - 1e-6:
                        continue
                except Exception:
                    pass
                try:
                    den = float(getattr(n, "scroll_end", 0.0)) - float(getattr(n, "scroll_hit", 0.0))
                    if abs(float(den)) > 1e-6:
                        if (float(sc_now) - float(getattr(n, "scroll_end", 0.0))) * float(den) >= -1e-6:
                            continue
                except Exception:
                    pass

            # Render hold notes
            if int(n.kind) == 3:
                self._render_hold_note(
                    s, n, ln, t, lx, ly, lr, sc_now, note_alpha,
                    W, H, expand, hold_body_w, outline_w,
                    no_cull_all, no_cull_screen, args
                )
                note_render_count += 1
                continue

            # Render regular notes (tap, drag, flick)
            self._render_regular_note(
                n, lx, ly, lr, sc_now, note_alpha,
                base_note_w, base_note_h, note_scale_x, note_scale_y,
                W, H, expand, no_cull_all, no_cull_screen,
                respack, draw_quad_fn, draw_textured_quad_pts_fn
            )
            note_render_count += 1

        return note_render_count

    def _render_hold_note(
        self,
        s: Any,
        n: Any,
        ln: Any,
        t: float,
        lx: float,
        ly: float,
        lr: float,
        sc_now: float,
        note_alpha: float,
        W: int,
        H: int,
        expand: float,
        hold_body_w: float,
        outline_w: float,
        no_cull_all: bool,
        no_cull_screen: bool,
        args: Any,
    ):
        """Render a hold note."""
        draw_outline = bool(getattr(args, "note_outline", False)) if args else False
        if args and bool(getattr(args, "no_note_outline", False)):
            draw_outline = False

        # Calculate head and tail positions
        hit_for_draw = bool(getattr(s, "hit", False)) and (not bool(getattr(n, "fake", False)))

        if hit_for_draw:
            head = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
        else:
            head_target_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
            head = note_world_pos(lx, ly, lr, sc_now, n, head_target_scroll, for_tail=False)

        tail = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_end, for_tail=True)

        head_s = apply_expand_xy(float(head[0]), float(head[1]), W, H, expand)
        tail_s = apply_expand_xy(float(tail[0]), float(tail[1]), W, H, expand)

        # Screen-based culling
        if (not no_cull_all) and (not no_cull_screen):
            try:
                m = 140.0
                minx = min(float(head_s[0]), float(tail_s[0]))
                maxx = max(float(head_s[0]), float(tail_s[0]))
                miny = min(float(head_s[1]), float(tail_s[1]))
                maxy = max(float(head_s[1]), float(tail_s[1]))
                if maxx < -m or minx > float(W) + m or maxy < -m or miny > float(H) + m:
                    return
            except:
                pass

        # Calculate hold alpha
        hold_alpha = float(note_alpha)
        if bool(getattr(s, "hold_failed", False)):
            hold_alpha *= 0.35

        # Get properties
        mh = bool(getattr(n, "mh", False))
        size_scale = float(getattr(n, "size_px", 1.0) or 1.0)
        note_rgb = getattr(n, "tint_rgb", (255, 255, 255))
        line_rgb = ln.color_rgb

        # Calculate progress for visual feedback
        prog = None
        try:
            if bool(getattr(s, "hit", False)) or bool(getattr(s, "holding", False)):
                if float(sc_now) > float(n.scroll_hit) + 1e-6 and (float(n.scroll_end) > float(n.scroll_hit) + 1e-6):
                    den = float(n.scroll_end) - float(n.scroll_hit)
                    prog = clamp((float(sc_now) - float(n.scroll_hit)) / max(1e-6, den), 0.0, 1.0)
        except:
            prog = None

        # Get respack from context (passed via args or global)
        respack = getattr(args, "_respack", None) if args else None

        # Draw hold using hold renderer
        self.hold_renderer.draw_hold_3slice(
            overlay=self.hold_renderer.overlay,
            head_xy=(float(head_s[0]), float(head_s[1])),
            tail_xy=(float(tail_s[0]), float(tail_s[1])),
            line_rot=float(lr),
            alpha01=float(hold_alpha),
            line_rgb=(int(line_rgb[0]), int(line_rgb[1]), int(line_rgb[2])),
            note_rgb=(int(note_rgb[0]), int(note_rgb[1]), int(note_rgb[2])),
            size_scale=float(size_scale),
            mh=bool(mh),
            hold_body_w=hold_body_w,
            progress=prog,
            draw_outline=bool(draw_outline),
            outline_width=float(outline_w),
            respack=respack,
        )

    def _render_regular_note(
        self,
        n: Any,
        lx: float,
        ly: float,
        lr: float,
        sc_now: float,
        note_alpha: float,
        base_note_w: float,
        base_note_h: float,
        note_scale_x: float,
        note_scale_y: float,
        W: int,
        H: int,
        expand: float,
        no_cull_all: bool,
        no_cull_screen: bool,
        respack: Any,
        draw_quad_fn: Callable,
        draw_textured_quad_pts_fn: Callable,
    ):
        """Render a regular note (tap, drag, flick)."""
        # Calculate world position
        xw, yw = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
        xw, yw = apply_expand_xy(float(xw), float(yw), W, H, expand)

        # Screen-based culling
        if (not no_cull_all) and (not no_cull_screen):
            try:
                m = 140.0
                if float(xw) < -m or float(xw) > float(W) + m or float(yw) < -m or float(yw) > float(H) + m:
                    return
            except:
                pass

        # Calculate note dimensions
        size_scale = float(getattr(n, "size_px", 1.0) or 1.0)
        w = base_note_w * note_scale_x * float(size_scale)
        h = base_note_h * note_scale_y * float(size_scale)

        # Calculate color
        a_note = int(255 * float(note_alpha))
        tr, tg, tb = (255, 255, 255)
        try:
            tr, tg, tb = getattr(n, "tint_rgb", (255, 255, 255))
        except:
            tr, tg, tb = (255, 255, 255)

        # Get texture
        tex = self._pick_note_tex(n, respack)
        if tex is not None:
            try:
                tw, th = tex.size
                if int(tw) > 0 and int(th) > 0:
                    h = float(w) * float(th) / float(tw) * float(note_scale_y)
            except Exception:
                pass
        pts = rect_corners(float(xw), float(yw), float(w), float(h), float(lr))

        if tex is not None:
            # Render textured note
            draw_textured_quad_pts_fn(
                tex=tex,
                pts=pts,
                rgba=(int(tr), int(tg), int(tb), int(a_note)),
            )
        else:
            # Fallback to colored quad
            r0, g0, b0 = NOTE_TYPE_COLORS.get(int(n.kind), (255, 255, 255))
            draw_quad_fn(
                pts,
                (int(r0 * tr / 255), int(g0 * tg / 255), int(b0 * tb / 255), int(a_note))
            )

    def _pick_note_tex(self, note: Any, respack: Any) -> Optional[Any]:
        """Pick appropriate texture for note based on kind and mh flag."""
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
