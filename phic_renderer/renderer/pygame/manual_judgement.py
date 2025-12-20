from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Set, Tuple

from ...runtime.judge import Judge
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...types import NoteState, RuntimeLine, RuntimeNote
from .judge_helpers import apply_grade


@dataclass
class ManualJudgementConfig:
    judge_width_ratio: float


def _note_x_at_time(lines: List[RuntimeLine], n: RuntimeNote, tt: float) -> float:
    ln = lines[int(n.line_id)]
    lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, tt)
    scroll_target = float(n.scroll_hit)
    xw, yw = note_world_pos(lx, ly, lr, sc_now, n, scroll_target, for_tail=False)
    return float(xw)


def _in_judge_width(lines: List[RuntimeLine], n: RuntimeNote, tt: float, pointer_x: Optional[float], judge_w_px: float) -> bool:
    if pointer_x is None:
        return True
    try:
        nx = _note_x_at_time(lines, n, tt)
    except Exception:
        return True
    return abs(float(pointer_x) - float(nx)) <= float(judge_w_px) * 0.5


def _pick_best_candidate(
    *,
    states: List[NoteState],
    idx_next: int,
    allow_kinds: Set[int],
    t: float,
    pointer_x: Optional[float],
    judge_w_px: float,
    lines: List[RuntimeLine],
) -> Optional[NoteState]:
    best_s: Optional[NoteState] = None
    best_dt = 1e9
    st0 = max(0, int(idx_next) - 80)
    st1 = min(len(states), int(idx_next) + 900)
    for si in range(st0, st1):
        s = states[si]
        if s.judged or s.note.fake:
            continue
        n = s.note
        if int(n.kind) not in allow_kinds:
            continue
        dt = abs(float(t) - float(n.t_hit))
        if dt > float(Judge.BAD):
            continue
        if not _in_judge_width(lines, n, float(t), pointer_x, float(judge_w_px)):
            continue
        if dt < best_dt:
            best_dt = dt
            best_s = s
    return best_s


def apply_manual_judgement(
    *,
    args: Any,
    t: float,
    W: int,
    lines: List[RuntimeLine],
    states: List[NoteState],
    idx_next: int,
    judge: Any,
    record_enabled: bool,
    respack: Any,
    hitsound: Any,
    hitfx: List[Any],
    particles: List[Any],
    HitFX_cls: Any,
    ParticleBurst_cls: Any,
    hold_fx_interval_ms: int,
    mark_line_hit_cb: Any,
    push_hit_debug_cb: Any,
    pointer_id: int,
    pointer_x: Optional[float],
    gesture: Optional[str],
    hold_like_down: bool,
    press_edge: bool,
) -> None:
    try:
        judge_w_px = float(getattr(args, "judge_width", 0.12)) * float(W)
    except Exception:
        judge_w_px = 0.12 * float(W)
    if judge_w_px < 1.0:
        judge_w_px = 1.0

    # 1) discrete gesture judgement (tap/flick)
    if gesture is not None:
        if gesture == "tap":
            cand = _pick_best_candidate(
                states=states,
                idx_next=idx_next,
                allow_kinds={1},
                t=float(t),
                pointer_x=pointer_x,
                judge_w_px=float(judge_w_px),
                lines=lines,
            )
        elif gesture == "flick":
            cand = _pick_best_candidate(
                states=states,
                idx_next=idx_next,
                allow_kinds={4},
                t=float(t),
                pointer_x=pointer_x,
                judge_w_px=float(judge_w_px),
                lines=lines,
            )
        else:
            cand = None

        if cand is not None:
            n = cand.note
            grade = judge.grade_window(float(n.t_hit), float(t))
            if grade is not None:
                apply_grade(cand, str(grade), judge)
                ln = lines[n.line_id]
                lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                c = (255, 255, 255, 255)
                if getattr(n, "tint_hitfx_rgb", None) is not None:
                    rr, gg, bb = n.tint_hitfx_rgb
                    c = (int(rr), int(gg), int(bb), 255)
                elif respack:
                    c = (
                        respack.judge_colors.get(grade)
                        or respack.judge_colors.get("GOOD")
                        or respack.judge_colors.get("PERFECT")
                        or c
                    )
                hitfx.append(HitFX_cls(x, y, t, c, lr))
                if respack and (not respack.hide_particles):
                    particles.append(ParticleBurst_cls(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                mark_line_hit_cb(n.line_id, int(t * 1000.0))
                push_hit_debug_cb(
                    t_now=float(t),
                    t_hit=float(n.t_hit),
                    note_id=int(getattr(n, "nid", -1)),
                    judgement=str(grade),
                )
                if not record_enabled:
                    hitsound.play(n, int(t * 1000.0), respack=respack)

    # 2) continuous drag judgement: holding down can judge kind=2
    if hold_like_down:
        cand_drag = _pick_best_candidate(
            states=states,
            idx_next=idx_next,
            allow_kinds={2},
            t=float(t),
            pointer_x=pointer_x,
            judge_w_px=float(judge_w_px),
            lines=lines,
        )
        if cand_drag is not None:
            n = cand_drag.note
            dt = abs(float(t) - float(n.t_hit))
            if dt <= float(Judge.PERFECT):
                apply_grade(cand_drag, "PERFECT", judge)
                ln = lines[n.line_id]
                lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                c = (255, 255, 255, 255)
                if getattr(n, "tint_hitfx_rgb", None) is not None:
                    rr, gg, bb = n.tint_hitfx_rgb
                    c = (int(rr), int(gg), int(bb), 255)
                elif respack:
                    c = respack.judge_colors.get("PERFECT", c)
                hitfx.append(HitFX_cls(x, y, t, c, lr))
                if respack and (not respack.hide_particles):
                    particles.append(ParticleBurst_cls(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                mark_line_hit_cb(n.line_id, int(t * 1000.0))
                push_hit_debug_cb(
                    t_now=float(t),
                    t_hit=float(n.t_hit),
                    note_id=int(getattr(n, "nid", -1)),
                    judgement="PERFECT",
                )
                if not record_enabled:
                    hitsound.play(n, int(t * 1000.0), respack=respack)

    # 3) hold head judgement: press_edge triggers kind=3
    if press_edge:
        cand_hold = _pick_best_candidate(
            states=states,
            idx_next=idx_next,
            allow_kinds={3},
            t=float(t),
            pointer_x=pointer_x,
            judge_w_px=float(judge_w_px),
            lines=lines,
        )
        if cand_hold is not None:
            n = cand_hold.note
            dt = abs(float(t) - float(n.t_hit))
            grade_h = "PERFECT" if dt <= float(Judge.PERFECT) else ("GOOD" if dt <= float(Judge.BAD) else None)
            if grade_h is not None:
                cand_hold.hit = True
                cand_hold.holding = True
                cand_hold.hold_grade = str(grade_h)
                try:
                    setattr(cand_hold, "hold_pointer_id", int(pointer_id))
                except Exception:
                    pass
                judge.bump()
                cand_hold.next_hold_fx_ms = int(t * 1000.0) + int(hold_fx_interval_ms)
                ln = lines[n.line_id]
                lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, t)
                x, y = note_world_pos(lx, ly, lr, sc_now, n, n.scroll_hit, for_tail=False)
                c = (255, 255, 255, 255)
                if getattr(n, "tint_hitfx_rgb", None) is not None:
                    rr, gg, bb = n.tint_hitfx_rgb
                    c = (int(rr), int(gg), int(bb), 255)
                elif respack:
                    c = (
                        respack.judge_colors.get(grade_h)
                        or respack.judge_colors.get("GOOD")
                        or respack.judge_colors.get("PERFECT")
                        or c
                    )
                hitfx.append(HitFX_cls(x, y, t, c, lr))
                if respack and (not respack.hide_particles):
                    particles.append(ParticleBurst_cls(x, y, int(t * 1000.0), int(respack.hitfx_duration * 1000), c))
                mark_line_hit_cb(n.line_id, int(t * 1000.0))
                push_hit_debug_cb(
                    t_now=float(t),
                    t_hit=float(n.t_hit),
                    note_id=int(getattr(n, "nid", -1)),
                    judgement=str(grade_h),
                    hold_percent=0.0,
                )
                if not record_enabled:
                    hitsound.play(n, int(t * 1000.0), respack=respack)
