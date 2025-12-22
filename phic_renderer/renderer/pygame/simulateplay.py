from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ...runtime.judge import Judge
from ...runtime.kinematics import eval_line_state, note_world_pos
from ...types import NoteState, RuntimeLine, RuntimeNote


@dataclass
class _SimPointerAssign:
    kind: str  # 'hold' | 'drag' | 'flick'
    note_id: int
    release_at: Optional[float] = None
    down_at: Optional[float] = None
    up_at: Optional[float] = None
    start_x: Optional[float] = None
    start_y: Optional[float] = None
    flick_to_y: Optional[float] = None


class SimulatePlayer:
    def __init__(self, *, mode: str = "conservative", max_pointers: int = 2):
        self.mode = str(mode or "conservative").strip().lower()
        if self.mode not in {"conservative", "aggressive", "extreme"}:
            self.mode = "conservative"
        self.max_pointers = max(0, int(max_pointers))

        # We intentionally avoid pointer_id=0 because it's reserved for real mouse.
        self.pointer_ids: List[int] = [i + 1 for i in range(self.max_pointers)]
        self._assign: Dict[int, _SimPointerAssign] = {}

        self._cooldown_until: Dict[int, float] = {int(pid): -1e9 for pid in self.pointer_ids}

        self._t_prev: Optional[float] = None
        self._dt_frame_est: float = 1.0 / 60.0

    def _dt_max(self) -> float:
        return float(Judge.BAD if self.mode in {"aggressive", "extreme"} else Judge.PERFECT)

    def _update_frame_dt(self, *, t: float) -> None:
        try:
            if self._t_prev is None:
                self._t_prev = float(t)
                return
            dt = float(t) - float(self._t_prev)
            self._t_prev = float(t)
            if dt <= 0.0:
                return
            dt = max(1.0 / 240.0, min(1.0 / 10.0, float(dt)))
            self._dt_frame_est = float(self._dt_frame_est) * 0.85 + float(dt) * 0.15
        except Exception:
            return

    def _should_fire_now(self, *, t: float, t_hit: float) -> bool:
        try:
            if float(t) >= float(t_hit):
                return True
            thr = max(1e-6, float(self._dt_frame_est) * 0.5)
            return (float(t_hit) - float(t)) <= float(thr)
        except Exception:
            return True

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
                try:
                    self._cooldown_until[int(pid)] = float(self._t_prev or 0.0) + 0.06
                except Exception:
                    pass
                return int(pid)

        if self.mode == "extreme":
            for pid, asg in list(self._assign.items()):
                if asg is not None and str(asg.kind) == "flick":
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    try:
                        self._assign.pop(int(pid), None)
                    except Exception:
                        pass
                    try:
                        self._cooldown_until[int(pid)] = float(self._t_prev or 0.0) + 0.06
                    except Exception:
                        pass
                    return int(pid)

        if self.mode in {"aggressive", "extreme"}:
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
        now_t = float(self._t_prev or 0.0)
        best_pid = None
        best_cd = 1e9
        for pid in self.pointer_ids:
            if self._assign.get(int(pid)) is not None:
                continue
            try:
                if bool(pointers.is_down(int(pid))):
                    continue
            except Exception:
                pass
            cd = float(self._cooldown_until.get(int(pid), -1e9))
            if float(now_t) >= float(cd):
                return int(pid)
            if float(cd) < float(best_cd):
                best_cd = float(cd)
                best_pid = int(pid)
        if best_pid is not None:
            return int(best_pid)
        return self._try_preempt_one(pointers=pointers)

    def _pos_at(self, lines: List[RuntimeLine], n: RuntimeNote, t: float) -> Tuple[float, float]:
        ln = lines[int(n.line_id)]
        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
        scroll_target = float(getattr(n, "scroll_hit", 0.0) or 0.0)
        xw, yw = note_world_pos(float(lx), float(ly), float(lr), float(sc_now), n, float(scroll_target), for_tail=False)
        return float(xw), float(yw)

    def _hold_head_pos_at(self, lines: List[RuntimeLine], n: RuntimeNote, t: float) -> Tuple[float, float]:
        ln = lines[int(n.line_id)]
        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
        sh = float(getattr(n, "scroll_hit", 0.0) or 0.0)
        scroll_target = float(sh) if float(sc_now) <= float(sh) else float(sc_now)
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

    def _clamp01(self, v: float) -> float:
        v = float(v)
        if v <= 0.0:
            return 0.0
        if v >= 1.0:
            return 1.0
        return v

    def _ease_out_quad(self, p: float) -> float:
        p = self._clamp01(p)
        return 1.0 - (1.0 - p) * (1.0 - p)

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

        self._update_frame_dt(t=float(t))

        dt_max = float(self._dt_max())
        dt_discrete = float(Judge.BAD)

        claimed_note_ids = set()
        for asg in list(self._assign.values()):
            if asg is None:
                continue
            try:
                claimed_note_ids.add(int(asg.note_id))
            except Exception:
                pass

        # Release finished / scheduled pointers first.
        for pid in list(self.pointer_ids):
            asg = self._assign.get(int(pid))
            if asg is None:
                continue
            if asg.kind == "hold":
                # Find the note by nid (stable across sorts)
                n_end = None
                n_obj: Optional[RuntimeNote] = None
                for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 120, idx_next + 1200)):
                    if int(getattr(s.note, "nid", -1)) == int(asg.note_id):
                        n_end = float(getattr(s.note, "t_end", 0.0))
                        n_obj = s.note
                        break
                if n_obj is not None:
                    try:
                        xh, yh = self._hold_head_pos_at(lines, n_obj, float(t))
                        pointers.sim_move(int(pid), float(xh), float(yh))
                    except Exception:
                        pass
                if n_end is not None and float(t) >= float(n_end):
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)
                    try:
                        self._cooldown_until[int(pid)] = float(t) + 0.08
                    except Exception:
                        pass
            elif asg.kind == "drag":
                if asg.release_at is not None and float(t) >= float(asg.release_at):
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)
                    try:
                        self._cooldown_until[int(pid)] = float(t) + 0.06
                    except Exception:
                        pass
            elif asg.kind == "flick":
                try:
                    if not bool(pointers.is_down(int(pid))):
                        self._assign.pop(int(pid), None)
                        continue
                except Exception:
                    pass
                if asg.up_at is None or asg.down_at is None:
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)
                    continue
                if asg.start_x is None or asg.start_y is None or asg.flick_to_y is None:
                    try:
                        pointers.sim_up(int(pid), no_gesture=True)
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)
                    continue

                denom = max(1e-6, float(asg.up_at) - float(asg.down_at))
                p = self._ease_out_quad((float(t) - float(asg.down_at)) / float(denom))
                try:
                    x_base = float(asg.start_x)
                    y_base = float(asg.start_y)
                    try:
                        n_now: Optional[RuntimeNote] = None
                        for ss in self._candidate_iter(states, idx_next, st_range=(idx_next - 120, idx_next + 1200)):
                            if int(getattr(ss.note, "nid", -1)) == int(asg.note_id):
                                n_now = ss.note
                                break
                        if n_now is not None:
                            xx, yy = self._pos_at(lines, n_now, float(t))
                            x_base = float(xx)
                            y_base = float(yy)
                    except Exception:
                        pass
                    if float(p) <= 0.5:
                        q = self._ease_out_quad(float(p) * 2.0)
                        y_now = float(y_base) + (float(asg.flick_to_y) - float(y_base)) * float(q)
                    else:
                        q = self._ease_out_quad((float(p) - 0.5) * 2.0)
                        y_now = float(asg.flick_to_y) + (float(y_base) - float(asg.flick_to_y)) * float(q)
                    pointers.sim_move(int(pid), float(x_base), float(y_now))
                except Exception:
                    pass
                if float(t) >= float(asg.up_at):
                    try:
                        pointers.sim_up(int(pid))
                    except Exception:
                        pass
                    self._assign.pop(int(pid), None)
                    try:
                        self._cooldown_until[int(pid)] = float(t) + 0.06
                    except Exception:
                        pass

        # 1) Start holds (occupies a pointer until end)
        for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 60, idx_next + 900)):
            n = s.note
            if int(n.kind) != 3:
                continue
            try:
                if int(getattr(n, "nid", -1)) in claimed_note_ids:
                    continue
            except Exception:
                pass
            if bool(getattr(s, "holding", False)) or bool(getattr(s, "hit", False)):
                continue
            if abs(float(t) - float(n.t_hit)) > float(dt_max):
                continue
            if not self._should_fire_now(t=float(t), t_hit=float(n.t_hit)):
                continue

            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break
            try:
                x, y = self._hold_head_pos_at(lines, n, float(t))
            except Exception:
                x, y = float(W) * 0.5, float(H) * 0.5
            try:
                pointers.sim_down(int(pid), float(x), float(y))
            except Exception:
                pass
            self._assign[int(pid)] = _SimPointerAssign(kind="hold", note_id=int(getattr(n, "nid", -1)))
            claimed_note_ids.add(int(getattr(n, "nid", -1)))

        # 2) Hit flick/tap with real pointer down/move/up
        for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 60, idx_next + 900)):
            n = s.note
            kd = int(n.kind)
            if kd not in (1, 4):
                continue
            if abs(float(t) - float(n.t_hit)) > float(dt_discrete):
                continue
            nid = int(getattr(n, "nid", -1))
            if nid in claimed_note_ids:
                continue

            if kd == 1:
                if not self._should_fire_now(t=float(t), t_hit=float(n.t_hit)):
                    continue
            else:
                try:
                    prepare = max(float(self._dt_frame_est) * 3.0, 0.04)
                except Exception:
                    prepare = 0.04
                if float(t) < float(n.t_hit) and (float(n.t_hit) - float(t)) > float(prepare):
                    continue

            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break
            claimed_note_ids.add(int(nid))
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
                thr_px = float(thr_ratio) * float(min(int(W), int(H)))
                dist = max(12.0, float(thr_px) * 1.8)
                down_at = float(t)
                try:
                    up_at = float(n.t_hit)
                except Exception:
                    up_at = float(t)
                if float(up_at) < float(down_at):
                    up_at = float(down_at)
                if abs(float(up_at) - float(down_at)) <= 1e-6:
                    try:
                        pointers.sim_move(int(pid), float(x), float(y) + float(dist))
                    except Exception:
                        pass
                    try:
                        pointers.sim_up(int(pid))
                    except Exception:
                        pass
                    try:
                        self._cooldown_until[int(pid)] = float(t) + 0.06
                    except Exception:
                        pass
                else:
                    self._assign[int(pid)] = _SimPointerAssign(
                        kind="flick",
                        note_id=int(nid),
                        down_at=float(down_at),
                        up_at=float(up_at),
                        start_x=float(x),
                        start_y=float(y),
                        flick_to_y=float(y) + float(dist),
                    )
                    claimed_note_ids.add(int(nid))
            else:
                try:
                    pointers.sim_up(int(pid))
                except Exception:
                    pass
                try:
                    self._cooldown_until[int(pid)] = float(t) + 0.04
                except Exception:
                    pass

        # 3) Hit drags: press down briefly near hit time
        # Need at least one frame of 'down' for apply_manual_judgement to judge kind=2.
        for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 60, idx_next + 900)):
            n = s.note
            if int(n.kind) != 2:
                continue
            try:
                if int(getattr(n, "nid", -1)) in claimed_note_ids:
                    continue
            except Exception:
                pass
            try:
                drag_prepare = max(float(self._dt_frame_est) * 2.5, 0.04)
            except Exception:
                drag_prepare = 0.04
            if float(t) < float(n.t_hit):
                if (float(n.t_hit) - float(t)) > float(drag_prepare):
                    continue
            else:
                if (float(t) - float(n.t_hit)) > float(Judge.PERFECT):
                    continue

            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break
            claimed_note_ids.add(int(getattr(n, "nid", -1)))
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
