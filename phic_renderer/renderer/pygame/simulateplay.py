from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ...runtime.judge import Judge
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...types import NoteState, RuntimeLine, RuntimeNote


@dataclass
class _SimPointerAssign:
    kind: str  # 'hold' | 'drag'
    note_id: int
    release_at: Optional[float] = None


class SimulatePlayer:
    def __init__(self, *, mode: str = "conservative", max_pointers: int = 2):
        self.mode = str(mode or "conservative").strip().lower()
        if self.mode not in {"conservative", "aggressive"}:
            self.mode = "conservative"
        self.max_pointers = max(0, int(max_pointers))

        # We intentionally avoid pointer_id=0 because it's reserved for real mouse.
        self.pointer_ids: List[int] = [i + 1 for i in range(self.max_pointers)]
        self._assign: Dict[int, _SimPointerAssign] = {}

    def _dt_max(self) -> float:
        return float(Judge.BAD if self.mode == "aggressive" else Judge.PERFECT)

    def _try_preempt_one(self, *, pointers: Any) -> Optional[int]:
        # Prefer preempting short-lived actions first.
        for pid, asg in list(self._assign.items()):
            if asg is not None and str(asg.kind) == "drag":
                try:
                    pointers.sim_up(int(pid), no_gesture=True)
                except Exception:
                    pass
                try:
                    self._assign.pop(int(pid), None)
                except Exception:
                    pass
                return int(pid)

        if self.mode == "aggressive":
            for pid, asg in list(self._assign.items()):
                if asg is not None and str(asg.kind) == "hold":
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    try:
                        self._assign.pop(int(pid), None)
                    except Exception:
                        pass
                    return int(pid)

        return None

    def _acquire_pid(self, *, pointers: Any) -> Optional[int]:
        for pid in self.pointer_ids:
            if self._assign.get(int(pid)) is not None:
                continue
            try:
                if bool(pointers.is_down(int(pid))):
                    continue
            except Exception:
                pass
            return int(pid)
        return self._try_preempt_one(pointers=pointers)

    def _pos_at(self, lines: List[RuntimeLine], n: RuntimeNote, t: float) -> Tuple[float, float]:
        ln = lines[int(n.line_id)]
        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
        scroll_target = float(getattr(n, "scroll_hit", 0.0) or 0.0)
        xw, yw = note_world_pos(float(lx), float(ly), float(lr), float(sc_now), n, float(scroll_target), for_tail=False)
        return float(xw), float(yw)

    def _candidate_iter(self, states: List[NoteState], idx_next: int, *, st_range: Tuple[int, int]):
        st0, st1 = st_range
        st0 = max(0, int(st0))
        st1 = min(len(states), int(st1))
        for si in range(st0, st1):
            s = states[si]
            if s.judged or s.note.fake:
                continue
            yield s

    def step(
        self,
        *,
        t: float,
        W: int,
        H: int,
        lines: List[RuntimeLine],
        states: List[NoteState],
        idx_next: int,
        pointers: Any,
    ) -> None:
        if self.max_pointers <= 0:
            return

        dt_max = float(self._dt_max())

        # Release finished / scheduled pointers first.
        for pid in list(self.pointer_ids):
            asg = self._assign.get(int(pid))
            if asg is None:
                continue
            if asg.kind == "hold":
                # Find the note by nid (stable across sorts)
                n_end = None
                for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 120, idx_next + 1200)):
                    if int(getattr(s.note, "nid", -1)) == int(asg.note_id):
                        n_end = float(getattr(s.note, "t_end", 0.0))
                        break
                if n_end is not None and float(t) >= float(n_end):
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)
            elif asg.kind == "drag":
                if asg.release_at is not None and float(t) >= float(asg.release_at):
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)

        # 1) Start holds (occupies a pointer until end)
        for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 60, idx_next + 900)):
            n = s.note
            if int(n.kind) != 3:
                continue
            if bool(getattr(s, "holding", False)) or bool(getattr(s, "hit", False)):
                continue
            if abs(float(t) - float(n.t_hit)) > float(dt_max):
                continue

            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break
            try:
                x, y = self._pos_at(lines, n, float(t))
            except Exception:
                x, y = float(W) * 0.5, float(H) * 0.5
            try:
                pointers.sim_down(int(pid), float(x), float(y))
            except Exception:
                pass
            self._assign[int(pid)] = _SimPointerAssign(kind="hold", note_id=int(getattr(n, "nid", -1)))

        # 2) Hit flick/tap with real pointer down/move/up
        for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 60, idx_next + 900)):
            n = s.note
            kd = int(n.kind)
            if kd not in (1, 4):
                continue
            if abs(float(t) - float(n.t_hit)) > float(dt_max):
                continue

            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break
            try:
                x, y = self._pos_at(lines, n, float(t))
            except Exception:
                x, y = float(W) * 0.5, float(H) * 0.5
            try:
                pointers.sim_down(int(pid), float(x), float(y))
            except Exception:
                pass

            if kd == 4:
                try:
                    thr_ratio = float(getattr(pointers, "flick_threshold_ratio", 0.02) or 0.02)
                except Exception:
                    thr_ratio = 0.02
                dist = max(8.0, float(thr_ratio) * float(min(int(W), int(H))) * 1.25)
                try:
                    pointers.sim_move(int(pid), float(x), float(y) + float(dist))
                except Exception:
                    pass
            try:
                pointers.sim_up(int(pid))
            except Exception:
                pass

        # 3) Hit drags: press down briefly near hit time
        # Need at least one frame of 'down' for apply_manual_judgement to judge kind=2.
        for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 60, idx_next + 900)):
            n = s.note
            if int(n.kind) != 2:
                continue
            # drag judgement requires PERFECT window in manual_judgement
            if abs(float(t) - float(n.t_hit)) > float(Judge.PERFECT if self.mode == "conservative" else Judge.BAD):
                continue

            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break
            try:
                x, y = self._pos_at(lines, n, float(t))
            except Exception:
                x, y = float(W) * 0.5, float(H) * 0.5
            try:
                pointers.sim_down(int(pid), float(x), float(y))
            except Exception:
                pass
            # Release shortly after; next frame will perform the release.
            self._assign[int(pid)] = _SimPointerAssign(kind="drag", note_id=int(getattr(n, "nid", -1)), release_at=float(t) + 0.06)
