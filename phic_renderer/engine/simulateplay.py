from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple
import bisect

from ..runtime.judge import Judge
from ..runtime.kinematics import eval_line_state, note_world_pos
from ..types import NoteState, RuntimeLine, RuntimeNote


@dataclass
class _SimPointerAssign:
    kind: str  # 'hold' | 'drag' | 'flick'
    note_id: int
    note_ref: Optional[RuntimeNote] = None  # Cache note reference
    release_at: Optional[float] = None
    down_at: Optional[float] = None
    up_at: Optional[float] = None
    start_x: Optional[float] = None
    start_y: Optional[float] = None
    flick_to_y: Optional[float] = None
    # Lazy movement optimization
    last_move_t: Optional[float] = None  # Last time pointer moved
    reusable: bool = True  # Can be reused for nearby notes


@dataclass
class _NoteTask:
    """Represents a scheduled note task with priority"""
    t_hit: float
    note_id: int
    note_kind: int
    priority: float  # Lower = higher priority

    def __lt__(self, other: '_NoteTask') -> bool:
        # First compare by time, then by priority
        if abs(self.t_hit - other.t_hit) < 1e-6:
            return self.priority < other.priority
        return self.t_hit < other.t_hit


class SimulatePlayer:
    """Optimized simulate player with intelligent pointer allocation"""

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

        # Optimization: Cache hold note references
        self._hold_cache: Dict[int, RuntimeNote] = {}

        # Optimization: Track last processed index
        self._last_idx_next: int = 0

        # Optimization: Precomputed timing windows
        self._timing_windows = {
            "conservative": Judge.PERFECT,
            "aggressive": Judge.BAD,
            "extreme": Judge.BAD
        }

    def _dt_max(self) -> float:
        """Get maximum timing window for current mode"""
        return float(self._timing_windows.get(self.mode, Judge.PERFECT))

    def _update_frame_dt(self, *, t: float) -> None:
        """Update frame time estimation with exponential smoothing"""
        try:
            if self._t_prev is None:
                self._t_prev = float(t)
                return
            dt = float(t) - float(self._t_prev)
            self._t_prev = float(t)
            if dt <= 0.0:
                return
            # Clamp to reasonable range
            dt = max(1.0 / 240.0, min(1.0 / 10.0, float(dt)))
            # Exponential smoothing
            self._dt_frame_est = float(self._dt_frame_est) * 0.85 + float(dt) * 0.15
        except Exception:
            return

    def _should_fire_now(self, *, t: float, t_hit: float, lookahead: float = 0.5) -> bool:
        """Determine if note should be triggered now with adaptive lookahead"""
        try:
            if float(t) >= float(t_hit):
                return True
            # Adaptive threshold based on frame rate
            thr = max(1e-6, float(self._dt_frame_est) * float(lookahead))
            return (float(t_hit) - float(t)) <= float(thr)
        except Exception:
            return True

    def _get_pointer_priority(self, asg: _SimPointerAssign) -> int:
        """Get priority for pointer preemption (lower = preempt first)"""
        kind = str(asg.kind)
        if kind == "drag":
            return 0  # Highest priority to preempt
        elif kind == "flick":
            return 1 if self.mode == "extreme" else 3
        elif kind == "hold":
            return 2 if self.mode in {"aggressive", "extreme"} else 4
        return 5

    def _try_preempt_one(self, *, pointers: Any) -> Optional[int]:
        """Intelligently preempt a pointer based on priority"""
        # Sort assignments by preemption priority
        candidates = []
        for pid, asg in self._assign.items():
            if asg is not None:
                priority = self._get_pointer_priority(asg)
                candidates.append((priority, pid, asg))

        if not candidates:
            return None

        # Preempt the lowest priority assignment
        candidates.sort(key=lambda x: x[0])
        priority, pid, asg = candidates[0]

        # Don't preempt if priority is too high (only preempt drag/flick/hold based on mode)
        if priority >= 4:
            return None

        try:
            pointers.sim_up(int(pid), no_gesture=True)
        except Exception:
            pass
        try:
            self._assign.pop(int(pid), None)
        except Exception:
            pass
        try:
            # Dynamic cooldown based on note type
            cooldown = 0.06 if asg.kind == "drag" else 0.08
            self._cooldown_until[int(pid)] = float(self._t_prev or 0.0) + cooldown
        except Exception:
            pass

        # Remove from hold cache if applicable
        if asg.kind == "hold" and asg.note_id in self._hold_cache:
            self._hold_cache.pop(asg.note_id, None)

        return int(pid)

    def _acquire_pid(self, *, pointers: Any) -> Optional[int]:
        """Acquire an available pointer ID"""
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
        """Calculate note position at given time"""
        ln = lines[int(n.line_id)]
        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
        scroll_target = float(getattr(n, "scroll_hit", 0.0) or 0.0)
        xw, yw = note_world_pos(float(lx), float(ly), float(lr), float(sc_now), n, float(scroll_target), for_tail=False)
        return float(xw), float(yw)

    def _hold_head_pos_at(self, lines: List[RuntimeLine], n: RuntimeNote, t: float) -> Tuple[float, float]:
        """Calculate hold head position at given time"""
        ln = lines[int(n.line_id)]
        lx, ly, lr, _la01, sc_now, _la_raw = eval_line_state(ln, float(t))
        sh = float(getattr(n, "scroll_hit", 0.0) or 0.0)
        scroll_target = float(sh) if float(sc_now) <= float(sh) else float(sc_now)
        xw, yw = note_world_pos(float(lx), float(ly), float(lr), float(sc_now), n, float(scroll_target), for_tail=False)
        return float(xw), float(yw)

    def _get_scan_range(self, idx_next: int, note_kind: int) -> Tuple[int, int]:
        """Get optimized scan range based on note type"""
        if note_kind == 3:  # Hold - wider range
            return (max(0, idx_next - 60), idx_next + 900)
        else:  # Tap/Drag/Flick - narrower range
            return (max(0, idx_next - 40), idx_next + 600)

    def _candidate_iter(self, states: List[NoteState], idx_next: int, *, st_range: Tuple[int, int]):
        """Iterate over candidate notes in range"""
        st0, st1 = st_range
        st0 = max(0, int(st0))
        st1 = min(len(states), int(st1))
        for si in range(st0, st1):
            s = states[si]
            if s.judged or s.note.fake:
                continue
            yield s

    def _clamp01(self, v: float) -> float:
        """Clamp value to [0, 1]"""
        v = float(v)
        if v <= 0.0:
            return 0.0
        if v >= 1.0:
            return 1.0
        return v

    def _ease_out_quad(self, p: float) -> float:
        """Quadratic ease-out function"""
        p = self._clamp01(p)
        return 1.0 - (1.0 - p) * (1.0 - p)

    def _judge_width(self, W: int, mode_override: Optional[str] = None) -> float:
        """Get judge width based on mode"""
        mode = mode_override or self.mode
        if mode == "extreme":
            return float(W) * 0.15  # Wider for extreme
        elif mode == "aggressive":
            return float(W) * 0.12
        else:  # conservative
            return float(W) * 0.10

    def _judge_height(self, H: int, mode_override: Optional[str] = None) -> float:
        """Get judge height based on mode"""
        mode = mode_override or self.mode
        if mode == "extreme":
            return float(H) * 0.08  # Taller for extreme
        elif mode == "aggressive":
            return float(H) * 0.06
        else:  # conservative
            return float(H) * 0.05

    def _can_reuse_pointer(self, *, pid: int, target_x: float, target_y: float, W: int, H: int, pointers: Any, vertical_only: bool = False) -> bool:
        """Check if a pointer can be reused for a target position"""
        try:
            if not bool(pointers.is_down(int(pid))):
                return False
        except Exception:
            return False

        asg = self._assign.get(int(pid))
        if asg is None or not asg.reusable:
            return False

        # Get current pointer position
        try:
            ptr_x, ptr_y = pointers.get_position(int(pid))
        except Exception:
            return False

        judge_w = self._judge_width(int(W))
        judge_h = self._judge_height(int(H))

        # For vertical-only judgment (hold + drag combo), only check Y axis
        if vertical_only:
            return abs(float(ptr_y) - float(target_y)) <= float(judge_h) * 0.5

        # Full 2D judgment
        dx = abs(float(ptr_x) - float(target_x))
        dy = abs(float(ptr_y) - float(target_y))
        return dx <= float(judge_w) * 0.5 and dy <= float(judge_h) * 0.5

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
        """Main step function with optimized algorithm"""
        if self.max_pointers <= 0:
            return

        self._update_frame_dt(t=float(t))

        dt_max = float(self._dt_max())
        dt_discrete = float(Judge.BAD)

        # Track claimed notes efficiently
        claimed_note_ids = set(int(asg.note_id) for asg in self._assign.values() if asg is not None)

        # === Phase 1: Release finished/scheduled pointers ===
        for pid in list(self.pointer_ids):
            asg = self._assign.get(int(pid))
            if asg is None:
                continue

            if asg.kind == "hold":
                # Use cached note reference if available
                n_obj = asg.note_ref or self._hold_cache.get(asg.note_id)
                if n_obj is None:
                    # Fallback: search for note
                    for s in self._candidate_iter(states, idx_next, st_range=(idx_next - 120, idx_next + 1200)):
                        if int(getattr(s.note, "nid", -1)) == int(asg.note_id):
                            n_obj = s.note
                            self._hold_cache[asg.note_id] = n_obj
                            break

                if n_obj is not None:
                    n_end = float(getattr(n_obj, "t_end", 0.0))

                    # Check if hold is finished
                    if float(t) >= float(n_end):
                        try:
                            pointers.sim_up(int(pid), no_gesture=True)
                        except Exception:
                            pass
                        self._assign.pop(int(pid), None)
                        self._hold_cache.pop(asg.note_id, None)
                        try:
                            self._cooldown_until[int(pid)] = float(t) + 0.08
                        except Exception:
                            pass
                        continue

                    # NEW: Area-based hold judgment - any pointer in judge area counts
                    # This allows pointer switching during hold
                    try:
                        xh, yh = self._hold_head_pos_at(lines, n_obj, float(t))
                        judge_w = self._judge_width(int(W))
                        judge_h = self._judge_height(int(H))

                        # Check if ANY pointer is in the hold judgment area
                        pointer_in_area = False
                        for check_pid in self.pointer_ids:
                            if check_pid == pid:
                                continue  # Skip self
                            try:
                                if not bool(pointers.is_down(int(check_pid))):
                                    continue
                                px, py = pointers.get_position(int(check_pid))
                                if px is None or py is None:
                                    continue
                                # Check if in judgment area (vertical priority for aggressive/extreme)
                                if self.mode in {"aggressive", "extreme"}:
                                    # Vertical-only judgment
                                    if abs(float(py) - float(yh)) <= float(judge_h) * 0.5:
                                        pointer_in_area = True
                                        break
                                else:
                                    # Full 2D judgment for conservative
                                    if (abs(float(px) - float(xh)) <= float(judge_w) * 0.5 and
                                        abs(float(py) - float(yh)) <= float(judge_h) * 0.5):
                                        pointer_in_area = True
                                        break
                            except Exception:
                                continue

                        if pointer_in_area:
                            # Another pointer is holding, release this one and mark as reusable
                            try:
                                pointers.sim_up(int(pid), no_gesture=True)
                            except Exception:
                                pass
                            self._assign.pop(int(pid), None)
                            # Don't remove from hold_cache - still being held by another pointer
                            continue

                        # No other pointer found, continue tracking with this pointer
                        # Get current position
                        try:
                            cur_x, cur_y = pointers.get_position(int(pid))
                        except Exception:
                            cur_x, cur_y = float(xh), float(yh)

                        # Only move if Y distance exceeds threshold or in conservative mode
                        dy = abs(float(cur_y) - float(yh))

                        if self.mode == "conservative":
                            # Conservative: track precisely
                            pointers.sim_move(int(pid), float(xh), float(yh))
                        elif dy > float(judge_h) * 0.3:
                            # Aggressive/Extreme: only move Y when needed, keep X unchanged
                            pointers.sim_move(int(pid), float(cur_x), float(yh))
                        # else: no movement needed
                    except Exception:
                        pass

            elif asg.kind == "drag":
                if asg.release_at is not None and float(t) >= float(asg.release_at):
                    # Check if this was a reused hold pointer
                    suspended_hold_id = getattr(asg, "suspended_hold_note_id", None)
                    if suspended_hold_id is not None and self.mode in {"aggressive", "extreme"}:
                        # Resume the hold state instead of releasing
                        asg.kind = "hold"
                        asg.note_id = int(suspended_hold_id)
                        # Restore hold note ref from cache
                        asg.note_ref = self._hold_cache.get(int(suspended_hold_id))
                        asg.release_at = None
                        try:
                            delattr(asg, "suspended_hold_note_id")
                        except Exception:
                            pass
                        # Pointer stays down, continues tracking hold
                    else:
                        # Normal drag release
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

                if not all([asg.up_at, asg.down_at, asg.start_x, asg.start_y, asg.flick_to_y]):
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

                    # Try to update position from cached note
                    n_now = asg.note_ref
                    if n_now is None:
                        for ss in self._candidate_iter(states, idx_next, st_range=(idx_next - 120, idx_next + 1200)):
                            if int(getattr(ss.note, "nid", -1)) == int(asg.note_id):
                                n_now = ss.note
                                break

                    if n_now is not None:
                        try:
                            xx, yy = self._pos_at(lines, n_now, float(t))
                            x_base = float(xx)
                            y_base = float(yy)
                        except Exception:
                            pass

                    # Smooth flick motion
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

        # === Phase 2: Start holds (highest priority - occupies pointer until end) ===
        scan_range = self._get_scan_range(idx_next, 3)
        for s in self._candidate_iter(states, idx_next, st_range=scan_range):
            n = s.note
            if int(n.kind) != 3:
                continue

            nid = int(getattr(n, "nid", -1))
            if nid in claimed_note_ids:
                continue

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

            self._assign[int(pid)] = _SimPointerAssign(
                kind="hold",
                note_id=nid,
                note_ref=n  # Cache reference
            )
            self._hold_cache[nid] = n
            claimed_note_ids.add(nid)

        # === Phase 3: Hit flick/tap with optimized timing ===
        scan_range = self._get_scan_range(idx_next, 1)
        for s in self._candidate_iter(states, idx_next, st_range=scan_range):
            n = s.note
            kd = int(n.kind)
            if kd not in (1, 4):
                continue

            if abs(float(t) - float(n.t_hit)) > float(dt_discrete):
                continue

            nid = int(getattr(n, "nid", -1))
            if nid in claimed_note_ids:
                continue

            # Different timing logic for tap vs flick
            if kd == 1:  # Tap
                if not self._should_fire_now(t=float(t), t_hit=float(n.t_hit)):
                    continue
            else:  # Flick - needs preparation time
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

            if kd == 4:  # Flick
                try:
                    thr_ratio = float(getattr(pointers, "flick_threshold_ratio", 0.02) or 0.02)
                except Exception:
                    thr_ratio = 0.02

                thr_px = float(thr_ratio) * float(min(int(W), int(H)))
                dist = max(12.0, float(thr_px) * 1.8)
                down_at = float(t)
                up_at = max(float(n.t_hit), down_at)

                if abs(float(up_at) - float(down_at)) <= 1e-6:
                    # Instant flick
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
                    # Animated flick
                    self._assign[int(pid)] = _SimPointerAssign(
                        kind="flick",
                        note_id=int(nid),
                        note_ref=n,  # Cache reference
                        down_at=float(down_at),
                        up_at=float(up_at),
                        start_x=float(x),
                        start_y=float(y),
                        flick_to_y=float(y) + float(dist),
                    )
                    claimed_note_ids.add(int(nid))
            else:  # Tap
                try:
                    pointers.sim_up(int(pid))
                except Exception:
                    pass
                try:
                    self._cooldown_until[int(pid)] = float(t) + 0.04
                except Exception:
                    pass

        # === Phase 4: Hit drags with optimized preparation and pointer reuse ===
        scan_range = self._get_scan_range(idx_next, 2)
        for s in self._candidate_iter(states, idx_next, st_range=scan_range):
            n = s.note
            if int(n.kind) != 2:
                continue

            nid = int(getattr(n, "nid", -1))
            if nid in claimed_note_ids:
                continue

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

            try:
                x, y = self._pos_at(lines, n, float(t))
            except Exception:
                x, y = float(W) * 0.5, float(H) * 0.5

            # Optimization: Try to reuse an existing hold pointer if within vertical judgment
            # This is especially efficient for holds with drags in the same vertical area
            reused_pid = None
            if self.mode in {"aggressive", "extreme"}:
                for pid in self.pointer_ids:
                    asg = self._assign.get(int(pid))
                    if asg is None or asg.kind != "hold":
                        continue
                    # Check if pointer can be reused (vertical-only judgment for hold+drag)
                    if self._can_reuse_pointer(pid=int(pid), target_x=float(x), target_y=float(y), W=int(W), H=int(H), pointers=pointers, vertical_only=True):
                        reused_pid = int(pid)
                        # Convert hold to drag temporarily
                        old_note_id = asg.note_id
                        asg.kind = "drag"
                        asg.note_id = int(nid)
                        asg.note_ref = n
                        asg.release_at = float(t) + 0.06
                        asg.reusable = True
                        # Mark hold note as temporarily suspended (will resume after drag)
                        try:
                            setattr(asg, "suspended_hold_note_id", old_note_id)
                        except Exception:
                            pass
                        break

            if reused_pid is not None:
                # Successfully reused pointer, no need to acquire new one
                claimed_note_ids.add(int(nid))
                continue

            # No reusable pointer found, acquire new one
            pid = self._acquire_pid(pointers=pointers)
            if pid is None:
                break

            claimed_note_ids.add(int(nid))

            try:
                pointers.sim_down(int(pid), float(x), float(y))
            except Exception:
                pass

            # Schedule release
            self._assign[int(pid)] = _SimPointerAssign(
                kind="drag",
                note_id=int(nid),
                note_ref=n,  # Cache reference
                release_at=float(t) + 0.06
            )
