from __future__ import annotations

from typing import Any, List

from ...math.util import apply_expand_xy
from ...runtime.judge import Judge
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...types import NoteState, RuntimeLine
from .draw import draw_line_rgba, draw_ring


def draw_debug_judge_windows(
    *,
    display_frame: Any,
    args: Any,
    t: float,
    W: int,
    H: int,
    overrender: float,
    expand: float,
    lines: List[RuntimeLine],
    states: List[NoteState],
    idx_next: int,
    RW: int,
    RH: int,
):
    try:
        judge_w_px = float(getattr(args, "judge_width", 0.12)) * float(W)
    except Exception:
        judge_w_px = 0.12 * float(W)
    if judge_w_px < 1.0:
        judge_w_px = 1.0

    st0 = max(0, int(idx_next) - 60)
    st1 = min(len(states), int(idx_next) + 700)
    for si in range(st0, st1):
        s = states[si]
        if s.judged or s.note.fake:
            continue
        n = s.note
        if int(getattr(n, "kind", 1)) == 3 and getattr(s, "holding", False):
            continue

        dt = abs(float(t) - float(n.t_hit))
        if dt > float(Judge.BAD):
            continue

        ln = lines[n.line_id]
        lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, float(t))
        x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
        ps = apply_expand_xy(float(x) * float(overrender), float(y) * float(overrender), int(RW), int(RH), expand)

        half_w = float(judge_w_px) * 0.5
        p0 = apply_expand_xy((float(x) - half_w) * float(overrender), float(y) * float(overrender), int(RW), int(RH), expand)
        p1 = apply_expand_xy((float(x) + half_w) * float(overrender), float(y) * float(overrender), int(RW), int(RH), expand)

        a = int(120)
        col_bad = (255, 80, 80, a)
        col_good = (255, 210, 80, a)
        col_perf = (80, 220, 255, a)
        if dt <= float(Judge.BAD):
            draw_line_rgba(display_frame, p0, p1, col_bad, width=max(1, int(2 * overrender)))
        if dt <= float(Judge.GOOD):
            draw_line_rgba(display_frame, p0, p1, col_good, width=max(1, int(3 * overrender)))
        if dt <= float(Judge.PERFECT):
            draw_line_rgba(display_frame, p0, p1, col_perf, width=max(1, int(4 * overrender)))

        draw_ring(
            display_frame,
            float(ps[0]),
            float(ps[1]),
            int(6 * overrender),
            (255, 255, 255, 140),
            thickness=max(1, int(2 * overrender)),
        )
