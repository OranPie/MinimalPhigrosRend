from __future__ import annotations

from typing import Any, Optional

from .judge import JUDGE_WEIGHT
from ..types import NoteState


def sanitize_grade(note_kind: int, grade: Optional[str]) -> Optional[str]:
    """Sanitize a grade string based on note kind rules."""
    if grade is None:
        return None
    g = str(grade).upper()
    k = int(note_kind)
    if k in (2, 4):
        return "PERFECT" if g in ("PERFECT", "GOOD") else None
    if k == 3:
        if g == "PERFECT":
            return "PERFECT"
        if g in ("GOOD", "BAD"):
            return "GOOD"
        return None
    if k == 1:
        if g in ("PERFECT", "GOOD", "BAD"):
            return g
        return None
    return None


def apply_grade(s: NoteState, grade: str, judge: Any):
    """Apply a grade to a note state and update judge accordingly."""
    g = str(grade).upper()
    if g == "PERFECT":
        judge.bump()
        s.judged = True
        s.hit = True
        judge.acc_sum += JUDGE_WEIGHT.get("PERFECT", 1.0)
        judge.judged_cnt += 1
        return
    if g == "GOOD":
        judge.bump()
        s.judged = True
        s.hit = True
        judge.acc_sum += JUDGE_WEIGHT.get("GOOD", 0.6)
        judge.judged_cnt += 1
        return
    if g == "BAD":
        judge.break_combo()
        s.judged = True
        s.hit = True
        judge.acc_sum += JUDGE_WEIGHT.get("BAD", 0.0)
        judge.judged_cnt += 1
        return


def finalize_hold(
    s: NoteState,
    t: float,
    judge: Any,
    hold_tail_tol: float,
    miss_window: float,
) -> bool:
    """Handle hold note finalization logic. Returns True if finalized."""
    n = s.note
    if n.fake or n.kind != 3 or s.hold_finalized:
        return False

    if (not s.hit) and (not s.hold_failed) and (t > n.t_hit + float(miss_window)):
        s.hold_failed = True
        judge.break_combo()

    if s.released_early and (not s.hold_finalized):
        dur = max(1e-6, (n.t_end - n.t_hit))
        prog = (t - n.t_hit) / dur
        if prog < 0:
            prog = 0.0
        if prog > 1:
            prog = 1.0
        if prog < hold_tail_tol:
            s.hold_failed = True
            judge.break_combo()
        else:
            g = s.hold_grade or "PERFECT"
            judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
            judge.judged_cnt += 1
            s.hold_finalized = True
            return True

    if t >= n.t_end and (not s.hold_finalized):
        if s.hit and (not s.hold_failed):
            g = s.hold_grade or "PERFECT"
            judge.acc_sum += JUDGE_WEIGHT.get(g, 0.0)
            judge.judged_cnt += 1
        else:
            judge.mark_miss(s)
        s.hold_finalized = True
        s.judged = True
        return True

    return False


def check_hold_release(
    s: NoteState,
    t: float,
    input_down: bool,
    judge: Any,
    hold_tail_tol: float,
):
    """Check if a hold note was released early and handle accordingly."""
    n = s.note
    if n.kind != 3 or not s.holding:
        return
    
    if (not input_down) and t < n.t_end - 1e-6:
        try:
            dur = max(1e-6, float(n.t_end) - float(n.t_hit))
            prog_r = (float(t) - float(n.t_hit)) / dur
            if prog_r < 0:
                prog_r = 0.0
            if prog_r > 1:
                prog_r = 1.0
        except:
            prog_r = 0.0
        s.released_early = True
        try:
            setattr(s, "release_t", float(t))
            setattr(s, "release_percent", float(prog_r))
        except:
            pass
        if float(prog_r) < float(hold_tail_tol):
            try:
                setattr(s, "miss_t", float(t))
            except:
                pass
            s.miss = True
            s.judged = True
            s.hold_failed = True
            s.hold_finalized = True
            s.holding = False
            judge.mark_miss(s)
        else:
            s.holding = False
    
    if t >= n.t_end:
        s.holding = False


def detect_miss(s: NoteState, t: float, judge: Any, miss_window: float):
    """Check if a non-hold note has been missed."""
    if s.judged or s.note.fake:
        return
    if s.note.kind == 3:
        return
    if t > s.note.t_hit + miss_window:
        try:
            setattr(s, "miss_t", float(t))
        except:
            pass
        judge.mark_miss(s)
