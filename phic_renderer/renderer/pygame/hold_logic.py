from __future__ import annotations

from typing import Any, Callable, List, Optional

from ...math.util import clamp
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...types import NoteState, RuntimeLine


def hold_maintenance(
    *,
    states: List[NoteState],
    idx_next: int,
    t: float,
    hold_tail_tol: float,
    pointers: Any,
    judge: Any,
):
    st0 = max(0, int(idx_next) - 50)
    st1 = min(len(states), int(idx_next) + 500)
    for si in range(st0, st1):
        s = states[si]
        if s.judged or s.note.fake:
            continue
        n = s.note
        if n.kind == 3 and s.holding:
            pid = getattr(s, "hold_pointer_id", None)
            try:
                down_now = pointers.is_down(pid if pid is not None else None)
            except Exception:
                down_now = pointers.any_down()
            if (not down_now) and float(t) < float(n.t_end) - 1e-6:
                try:
                    dur = max(1e-6, float(n.t_end) - float(n.t_hit))
                    prog_r = clamp((float(t) - float(n.t_hit)) / dur, 0.0, 1.0)
                except Exception:
                    prog_r = 0.0
                s.released_early = True
                try:
                    setattr(s, "release_t", float(t))
                    setattr(s, "release_percent", float(prog_r))
                except Exception:
                    pass
                if float(prog_r) < float(hold_tail_tol):
                    try:
                        setattr(s, "miss_t", float(t))
                    except Exception:
                        pass
                    s.miss = True
                    s.judged = True
                    s.hold_failed = True
                    s.hold_finalized = True
                    s.holding = False
                    judge.mark_miss(s)
                else:
                    s.holding = False
            if float(t) >= float(n.t_end):
                s.holding = False


def hold_finalize(
    *,
    states: List[NoteState],
    idx_next: int,
    t: float,
    hold_tail_tol: float,
    miss_window: float,
    judge: Any,
    push_hit_debug_cb: Callable[..., Any],
):
    st0 = max(0, int(idx_next) - 200)
    st1 = min(len(states), int(idx_next) + 800)
    for si in range(st0, st1):
        s = states[si]
        n = s.note
        if n.fake or n.kind != 3 or s.hold_finalized:
            continue

        if (not s.hit) and (not s.hold_failed) and (float(t) > float(n.t_hit) + float(miss_window)):
            s.hold_failed = True
            judge.break_combo()

        if s.released_early and (not s.hold_finalized):
            dur = max(1e-6, (float(n.t_end) - float(n.t_hit)))
            prog = clamp((float(t) - float(n.t_hit)) / dur, 0.0, 1.0)
            if float(prog) < float(hold_tail_tol):
                s.hold_failed = True
                judge.break_combo()
            else:
                g = s.hold_grade or "PERFECT"
                judge.acc_sum += getattr(judge, "JUDGE_WEIGHT", {}).get(g, 0.0) if hasattr(judge, "JUDGE_WEIGHT") else 0.0
                try:
                    from ...runtime.judge import JUDGE_WEIGHT

                    judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                except Exception:
                    pass
                judge.judged_cnt += 1
                s.hold_finalized = True
                push_hit_debug_cb(
                    t_now=float(t),
                    t_hit=float(n.t_hit),
                    note_id=int(getattr(n, "nid", -1)),
                    judgement=str(g),
                    hold_percent=float(prog),
                    note_kind=int(getattr(n, "kind", 0) or 0),
                    mh=bool(getattr(n, "mh", False)),
                    line_id=int(getattr(n, "line_id", -1)),
                    source="hold_finalize",
                )

        if float(t) >= float(n.t_end) and (not s.hold_finalized):
            if s.hit and (not s.hold_failed):
                g = s.hold_grade or "PERFECT"
                try:
                    from ...runtime.judge import JUDGE_WEIGHT

                    judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
                except Exception:
                    pass
                judge.judged_cnt += 1
                dur = max(1e-6, (float(n.t_end) - float(n.t_hit)))
                prog = clamp((float(t) - float(n.t_hit)) / dur, 0.0, 1.0)
                push_hit_debug_cb(
                    t_now=float(t),
                    t_hit=float(n.t_hit),
                    note_id=int(getattr(n, "nid", -1)),
                    judgement=str(g),
                    hold_percent=float(prog),
                    note_kind=int(getattr(n, "kind", 0) or 0),
                    mh=bool(getattr(n, "mh", False)),
                    line_id=int(getattr(n, "line_id", -1)),
                    source="hold_finalize",
                )
            else:
                judge.mark_miss(s)
                try:
                    dur = max(1e-6, (float(n.t_end) - float(n.t_hit)))
                    prog = clamp((float(t) - float(n.t_hit)) / dur, 0.0, 1.0)
                except Exception:
                    prog = 0.0
                push_hit_debug_cb(
                    t_now=float(t),
                    t_hit=float(n.t_hit),
                    note_id=int(getattr(n, "nid", -1)),
                    judgement="MISS",
                    hold_percent=float(prog),
                    note_kind=int(getattr(n, "kind", 0) or 0),
                    mh=bool(getattr(n, "mh", False)),
                    line_id=int(getattr(n, "line_id", -1)),
                    source="hold_finalize",
                )
            s.hold_finalized = True
            s.judged = True


def hold_tick_fx(
    *,
    states: List[NoteState],
    idx_next: int,
    t: float,
    hold_fx_interval_ms: int,
    lines: List[RuntimeLine],
    respack: Any,
    hitfx: List[Any],
    particles: List[Any],
    HitFX_cls: Any,
    ParticleBurst_cls: Any,
    mark_line_hit_cb: Callable[[int, int], Any],
):
    if not respack:
        return

    now_tick = int(float(t) * 1000.0)
    st0 = max(0, int(idx_next) - 200)
    st1 = min(len(states), int(idx_next) + 800)
    for si in range(st0, st1):
        s = states[si]
        n = s.note
        if n.fake or n.kind != 3 or (not s.holding) or s.judged:
            continue
        if float(t) >= float(n.t_end):
            continue
        if s.next_hold_fx_ms <= 0:
            s.next_hold_fx_ms = now_tick + int(hold_fx_interval_ms)
            continue
        while now_tick >= s.next_hold_fx_ms and float(t) < float(n.t_end):
            ln = lines[n.line_id]
            lx, ly, lr, la01, sc_now, la_raw = eval_line_state(ln, float(t))
            x, y = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
            g = str(getattr(s, "hold_grade", None) or "PERFECT").upper()
            c = respack.judge_colors.get(g, respack.judge_colors.get("PERFECT", (255, 255, 255, 255)))
            if getattr(n, "tint_hitfx_rgb", None) is not None:
                try:
                    rr, gg, bb = n.tint_hitfx_rgb
                    c = (int(rr), int(gg), int(bb), 255)
                except Exception:
                    pass
            var = "good" if g == "GOOD" else ""
            hitfx.append(HitFX_cls(x, y, float(t), c, lr, var))
            if not respack.hide_particles:
                particles.append(
                    ParticleBurst_cls(x, y, int(float(t) * 1000.0), int(respack.hitfx_duration * 1000), c)
                )
            mark_line_hit_cb(n.line_id, int(float(t) * 1000.0))
            s.next_hold_fx_ms += int(hold_fx_interval_ms)
