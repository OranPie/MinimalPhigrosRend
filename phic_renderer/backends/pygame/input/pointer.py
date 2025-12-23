from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import pygame


_TOUCH_ID_OFFSET = 1_000_000


@dataclass
class PointerFrame:
    pointer_id: int
    x: Optional[float]
    y: Optional[float]
    down: bool
    press_edge: bool
    release_edge: bool
    gesture: Optional[str]
    start_x: Optional[float] = None
    start_y: Optional[float] = None
    moved_px: float = 0.0
    moved_y: float = 0.0


class _PointerState:
    def __init__(self):
        self.down: bool = False
        self.x: Optional[float] = None
        self.y: Optional[float] = None
        self.start_x: Optional[float] = None
        self.start_y: Optional[float] = None
        self._last_x: Optional[float] = None
        self._last_y: Optional[float] = None
        self.moved_px: float = 0.0
        self.moved_y: float = 0.0
        self.press_edge: bool = False
        self.release_edge: bool = False
        self.gesture: Optional[str] = None

    def begin_frame(self):
        self.press_edge = False
        self.release_edge = False
        self.gesture = None


class PointerManager:
    def __init__(self, W: int, H: int, flick_threshold_ratio: float):
        self.W = int(W)
        self.H = int(H)
        self.flick_threshold_ratio = float(flick_threshold_ratio)
        self._p: Dict[int, _PointerState] = {}
        self._kbd = _PointerState()

    def _flick_threshold_px(self) -> float:
        base = float(min(int(self.W), int(self.H)))
        return float(self.flick_threshold_ratio) * base

    def cancel_all(self):
        # Force release all pointers without generating any gesture.
        for st in self._p.values():
            if st.down:
                st.down = False
                st.release_edge = True
            st.gesture = None
        if self._kbd.down:
            self._kbd.down = False
            self._kbd.release_edge = True
        self._kbd.gesture = None

    def set_screen_size(self, W: int, H: int):
        self.W = int(W)
        self.H = int(H)

    def set_flick_threshold_ratio(self, v: float):
        self.flick_threshold_ratio = float(v)

    def begin_frame(self):
        for st in self._p.values():
            st.begin_frame()
        self._kbd.begin_frame()

    def set_keyboard_down(self, down: bool):
        down = bool(down)
        prev = bool(self._kbd.down)
        if down and (not prev):
            self._kbd.down = True
            self._kbd.press_edge = True
            self._kbd.gesture = "tap"
        elif (not down) and prev:
            self._kbd.down = False
            self._kbd.release_edge = True

    def _get(self, pointer_id: int) -> _PointerState:
        st = self._p.get(int(pointer_id))
        if st is None:
            st = _PointerState()
            self._p[int(pointer_id)] = st
        return st

    def _update_move(self, st: _PointerState, x: Optional[float], y: Optional[float]):
        if x is None or y is None:
            return
        try:
            if st._last_x is not None and st._last_y is not None:
                dx = float(x) - float(st._last_x)
                dy = float(y) - float(st._last_y)
                st.moved_px += float((dx * dx + dy * dy) ** 0.5)
        except Exception:
            pass
        try:
            if st.start_y is not None:
                st.moved_y = max(float(st.moved_y), abs(float(y) - float(st.start_y)))
        except Exception:
            pass
        st._last_x = float(x)
        st._last_y = float(y)
        st.x = float(x)
        st.y = float(y)

    def sim_down(self, pointer_id: int, x: Optional[float], y: Optional[float]) -> None:
        st = self._get(int(pointer_id))
        st.down = True
        st.press_edge = True
        st.release_edge = False
        st.gesture = None
        st.moved_px = 0.0
        st.moved_y = 0.0
        st._last_x = float(x) if x is not None else None
        st._last_y = float(y) if y is not None else None
        st.start_x = float(x) if x is not None else None
        st.start_y = float(y) if y is not None else None
        st.x = float(x) if x is not None else None
        st.y = float(y) if y is not None else None

    def sim_move(self, pointer_id: int, x: Optional[float], y: Optional[float]) -> None:
        st = self._get(int(pointer_id))
        if not st.down:
            return
        self._update_move(st, x, y)

    def sim_up(self, pointer_id: int, *, gesture: Optional[str] = None, no_gesture: bool = False) -> None:
        st = self._get(int(pointer_id))
        if not st.down:
            return
        st.down = False
        st.release_edge = True
        if bool(no_gesture):
            st.gesture = None
            return
        if gesture is not None:
            st.gesture = str(gesture)
            return
        thr_px = float(self._flick_threshold_px())
        is_flick = bool(float(st.moved_y) >= float(thr_px))
        st.gesture = "flick" if is_flick else "tap"

    def sim_gesture(self, pointer_id: int, x: Optional[float], y: Optional[float], *, gesture: str) -> None:
        st = self._get(int(pointer_id))
        st.down = False
        st.press_edge = False
        st.release_edge = True
        st.gesture = str(gesture)
        st.moved_px = 0.0
        st._last_x = float(x) if x is not None else None
        st._last_y = float(y) if y is not None else None
        st.x = float(x) if x is not None else None
        st.y = float(y) if y is not None else None

    def _px_from_finger(self, fx: float, fy: float) -> Tuple[float, float]:
        return float(fx) * float(self.W), float(fy) * float(self.H)

    def process_event(self, ev: pygame.event.Event):
        try:
            et = int(ev.type)
        except Exception:
            return

        # Focus lost / cancel: some platforms won't send FINGERUP when app loses focus.
        if et == getattr(pygame, "WINDOWFOCUSLOST", -1):
            self.cancel_all()
            return
        if et == getattr(pygame, "WINDOWEVENT", -1):
            try:
                if int(getattr(ev, "event", -1)) == int(getattr(pygame, "WINDOWEVENT_FOCUS_LOST", -2)):
                    self.cancel_all()
                    return
            except Exception:
                pass
        if et == getattr(pygame, "ACTIVEEVENT", -1):
            try:
                gain = int(getattr(ev, "gain", 1))
                state = int(getattr(ev, "state", 0))
                # When losing input focus (state bit varies by SDL version), we still cancel.
                if gain == 0 and state != 0:
                    self.cancel_all()
                    return
            except Exception:
                pass

        if et == pygame.MOUSEBUTTONDOWN:
            try:
                if int(getattr(ev, "button", 0)) != 1:
                    return
            except Exception:
                return
            st = self._get(0)
            st.down = True
            st.press_edge = True
            st.moved_px = 0.0
            st.moved_y = 0.0
            try:
                x, y = float(ev.pos[0]), float(ev.pos[1])
            except Exception:
                x = y = None
            st._last_x = x
            st._last_y = y
            st.start_x = x
            st.start_y = y
            st.x = x
            st.y = y
            return

        if et == pygame.MOUSEBUTTONUP:
            try:
                if int(getattr(ev, "button", 0)) != 1:
                    return
            except Exception:
                return
            st = self._get(0)
            if st.down:
                st.down = False
                st.release_edge = True
                thr_px = float(self._flick_threshold_px())
                is_flick = bool(float(st.moved_y) >= float(thr_px))
                st.gesture = "flick" if is_flick else "tap"
            return

        if et == pygame.MOUSEMOTION:
            st = self._get(0)
            if not st.down:
                return
            try:
                x, y = float(ev.pos[0]), float(ev.pos[1])
            except Exception:
                return
            self._update_move(st, x, y)
            return

        if et == getattr(pygame, "FINGERDOWN", -1):
            try:
                pid = _TOUCH_ID_OFFSET + int(getattr(ev, "finger_id"))
                fx, fy = float(getattr(ev, "x")), float(getattr(ev, "y"))
                x, y = self._px_from_finger(fx, fy)
            except Exception:
                return
            st = self._get(pid)
            st.down = True
            st.press_edge = True
            st.moved_px = 0.0
            st.moved_y = 0.0
            st._last_x = float(x)
            st._last_y = float(y)
            st.start_x = float(x)
            st.start_y = float(y)
            st.x = float(x)
            st.y = float(y)
            return

        if et == getattr(pygame, "FINGERUP", -1):
            try:
                pid = _TOUCH_ID_OFFSET + int(getattr(ev, "finger_id"))
            except Exception:
                return
            st = self._get(pid)
            if st.down:
                st.down = False
                st.release_edge = True
                thr_px = float(self._flick_threshold_px())
                is_flick = bool(float(st.moved_y) >= float(thr_px))
                st.gesture = "flick" if is_flick else "tap"
            return

        if et == getattr(pygame, "FINGERMOTION", -1):
            try:
                pid = _TOUCH_ID_OFFSET + int(getattr(ev, "finger_id"))
                fx, fy = float(getattr(ev, "x")), float(getattr(ev, "y"))
                x, y = self._px_from_finger(fx, fy)
            except Exception:
                return
            st = self._get(pid)
            if not st.down:
                return
            self._update_move(st, x, y)
            return

    def is_down(self, pointer_id: Optional[int]) -> bool:
        if pointer_id is None:
            return bool(self.any_down())
        if int(pointer_id) == -1:
            return bool(self._kbd.down)
        st = self._p.get(int(pointer_id))
        return bool(st.down) if st is not None else False

    def get_position(self, pointer_id: int) -> Tuple[Optional[float], Optional[float]]:
        """Get current position of a pointer"""
        if int(pointer_id) == -1:
            return (None, None)
        st = self._p.get(int(pointer_id))
        if st is None:
            return (None, None)
        return (st.x, st.y)

    def any_down(self) -> bool:
        if self._kbd.down:
            return True
        for st in self._p.values():
            if st.down:
                return True
        return False

    def frame_pointers(self) -> List[PointerFrame]:
        out: List[PointerFrame] = []
        if self._kbd.press_edge or self._kbd.release_edge or self._kbd.down or self._kbd.gesture is not None:
            out.append(
                PointerFrame(
                    pointer_id=-1,
                    x=None,
                    y=None,
                    down=bool(self._kbd.down),
                    press_edge=bool(self._kbd.press_edge),
                    release_edge=bool(self._kbd.release_edge),
                    gesture=self._kbd.gesture,
                    start_x=None,
                    start_y=None,
                    moved_px=0.0,
                    moved_y=0.0,
                )
            )
        for pid, st in self._p.items():
            if (not st.down) and (not st.press_edge) and (not st.release_edge) and (st.gesture is None):
                continue
            out.append(
                PointerFrame(
                    pointer_id=int(pid),
                    x=st.x,
                    y=st.y,
                    down=bool(st.down),
                    press_edge=bool(st.press_edge),
                    release_edge=bool(st.release_edge),
                    gesture=st.gesture,
                    start_x=st.start_x,
                    start_y=st.start_y,
                    moved_px=float(st.moved_px),
                    moved_y=float(st.moved_y),
                )
            )
        return out
