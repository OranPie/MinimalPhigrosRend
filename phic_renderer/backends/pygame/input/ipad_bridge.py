from __future__ import annotations

import io
import time
import threading
import urllib.request
from dataclasses import dataclass
from typing import Any, Dict, Optional, Tuple

import pygame


@dataclass
class _DevPointerState:
    down: bool = False
    last_move_send_t: float = -1e9


class IPadPointerBridge:
    def __init__(
        self,
        inner: Any,
        *,
        controller: Any,
        render_size: Tuple[int, int],
        move_hz: float = 25.0,
        preview_fps: float = 15.0,
        mjpeg_url: Optional[str] = None,
        viewport_n: Tuple[float, float, float, float] = (0.0, 0.0, 1.0, 1.0),
    ):
        self._inner = inner
        self._g = controller
        self._render_w = int(render_size[0])
        self._render_h = int(render_size[1])
        self._move_hz = float(move_hz)
        self._preview_fps = float(preview_fps)

        self._mjpeg_url = (str(mjpeg_url) if mjpeg_url else None)
        self._mjpeg_last_jpeg: Optional[bytes] = None
        self._mjpeg_thread: Optional[threading.Thread] = None
        self._mjpeg_stop = threading.Event()

        vx0, vy0, vx1, vy1 = viewport_n
        self._vx0 = float(vx0)
        self._vy0 = float(vy0)
        self._vx1 = float(vx1)
        self._vy1 = float(vy1)

        self._dev: Dict[int, _DevPointerState] = {}

        self._last_preview_t = -1e9
        self._preview_cache: Optional[pygame.Surface] = None
        self._preview_cache_size: Optional[Tuple[int, int]] = None

        try:
            self._dev_w, self._dev_h = self._g.screen_size()
        except Exception:
            self._dev_w, self._dev_h = 1, 1

        if self._mjpeg_url:
            self._mjpeg_thread = threading.Thread(target=self._mjpeg_loop, daemon=True)
            self._mjpeg_thread.start()

    def close(self):
        try:
            self._mjpeg_stop.set()
        except Exception:
            pass
        try:
            self._g.quit()
        except Exception:
            pass

    def _to_dev_px(self, x: float, y: float) -> Tuple[int, int]:
        fx = 0.0 if self._render_w <= 1 else float(x) / float(self._render_w - 1)
        fy = 0.0 if self._render_h <= 1 else float(y) / float(self._render_h - 1)
        fx = max(0.0, min(1.0, float(fx)))
        fy = max(0.0, min(1.0, float(fy)))
        vx0 = max(0.0, min(1.0, float(self._vx0)))
        vy0 = max(0.0, min(1.0, float(self._vy0)))
        vx1 = max(0.0, min(1.0, float(self._vx1)))
        vy1 = max(0.0, min(1.0, float(self._vy1)))
        if vx1 < vx0:
            vx0, vx1 = vx1, vx0
        if vy1 < vy0:
            vy0, vy1 = vy1, vy0
        dev_fx = vx0 + float(fx) * max(1e-9, (vx1 - vx0))
        dev_fy = vy0 + float(fy) * max(1e-9, (vy1 - vy0))
        dev_fx = max(0.0, min(1.0, float(dev_fx)))
        dev_fy = max(0.0, min(1.0, float(dev_fy)))
        dx = int(round(float(dev_fx) * float(max(0, int(self._dev_w) - 1))))
        dy = int(round(float(dev_fy) * float(max(0, int(self._dev_h) - 1))))
        return dx, dy

    def _mjpeg_loop(self):
        url = str(self._mjpeg_url)
        buf = b""
        while not self._mjpeg_stop.is_set():
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "MinimalPhigrosRend"})
                with urllib.request.urlopen(req, timeout=10) as resp:
                    while not self._mjpeg_stop.is_set():
                        chunk = resp.read(4096)
                        if not chunk:
                            break
                        buf += chunk
                        a = buf.find(b"\xff\xd8")
                        b = buf.find(b"\xff\xd9")
                        if a != -1 and b != -1 and b > a:
                            jpeg = buf[a : b + 2]
                            buf = buf[b + 2 :]
                            self._mjpeg_last_jpeg = bytes(jpeg)
            except Exception:
                time.sleep(0.5)

    def _name(self, pointer_id: int) -> str:
        return f"sp{int(pointer_id)}"

    def _dev_state(self, pointer_id: int) -> _DevPointerState:
        st = self._dev.get(int(pointer_id))
        if st is None:
            st = _DevPointerState()
            self._dev[int(pointer_id)] = st
        return st

    def begin_frame(self):
        return self._inner.begin_frame()

    def set_screen_size(self, W: int, H: int):
        self._render_w = int(W)
        self._render_h = int(H)
        return self._inner.set_screen_size(int(W), int(H))

    def set_flick_threshold_ratio(self, v: float):
        return self._inner.set_flick_threshold_ratio(float(v))

    def cancel_all(self):
        try:
            for pid, st in list(self._dev.items()):
                if st.down:
                    try:
                        self._g.pointer_up(self._name(int(pid)))
                    except Exception:
                        pass
                st.down = False
        except Exception:
            pass
        return self._inner.cancel_all()

    def process_event(self, ev: pygame.event.Event):
        return self._inner.process_event(ev)

    def sim_down(self, pointer_id: int, x: Optional[float], y: Optional[float]) -> None:
        self._inner.sim_down(int(pointer_id), x, y)
        if x is None or y is None:
            return
        st = self._dev_state(int(pointer_id))
        st.down = True
        try:
            dx, dy = self._to_dev_px(float(x), float(y))
            self._g.pointer_down(self._name(int(pointer_id)), int(dx), int(dy))
        except Exception:
            pass

    def sim_move(self, pointer_id: int, x: Optional[float], y: Optional[float]) -> None:
        self._inner.sim_move(int(pointer_id), x, y)
        if x is None or y is None:
            return
        st = self._dev_state(int(pointer_id))
        if not st.down:
            return
        now = time.perf_counter()
        min_dt = 0.0 if self._move_hz <= 1e-6 else 1.0 / float(self._move_hz)
        if (now - float(st.last_move_send_t)) < float(min_dt):
            return
        st.last_move_send_t = float(now)
        try:
            dx, dy = self._to_dev_px(float(x), float(y))
            self._g.pointer_move(self._name(int(pointer_id)), int(dx), int(dy))
        except Exception:
            pass

    def sim_up(self, pointer_id: int, *, gesture: Optional[str] = None, no_gesture: bool = False) -> None:
        self._inner.sim_up(int(pointer_id), gesture=gesture, no_gesture=bool(no_gesture))
        st = self._dev_state(int(pointer_id))
        if not st.down:
            return
        st.down = False
        try:
            self._g.pointer_up(self._name(int(pointer_id)))
        except Exception:
            pass

    def sim_gesture(self, pointer_id: int, x: Optional[float], y: Optional[float], *, gesture: str) -> None:
        self._inner.sim_gesture(int(pointer_id), x, y, gesture=str(gesture))
        try:
            if x is not None and y is not None:
                dx, dy = self._to_dev_px(float(x), float(y))
                if str(gesture) == "tap":
                    self._g.tap(int(dx), int(dy))
        except Exception:
            pass

    def is_down(self, pointer_id: Optional[int]) -> bool:
        return self._inner.is_down(pointer_id)

    def get_position(self, pointer_id: int):
        return self._inner.get_position(int(pointer_id))

    def any_down(self) -> bool:
        return self._inner.any_down()

    def frame_pointers(self):
        return self._inner.frame_pointers()

    def preview_surface(self, *, W: int, H: int) -> Optional[pygame.Surface]:
        if self._preview_fps <= 0:
            return None
        now = time.perf_counter()
        min_dt = 1.0 / float(max(1e-6, float(self._preview_fps)))
        if (now - float(self._last_preview_t)) < float(min_dt):
            if self._preview_cache is not None and self._preview_cache_size == (int(W), int(H)):
                return self._preview_cache
            return None
        self._last_preview_t = float(now)

        if self._mjpeg_url and self._mjpeg_last_jpeg:
            try:
                try:
                    from PIL import Image  # type: ignore
                except Exception as e:
                    raise RuntimeError("Pillow is required for MJPEG preview") from e
                img = Image.open(io.BytesIO(self._mjpeg_last_jpeg)).convert("RGB")
                sw, sh = img.size
                rgb = bytes(img.tobytes())
                surf = pygame.image.frombuffer(rgb, (int(sw), int(sh)), "RGB")
                surf = surf.copy()
                if int(sw) != int(W) or int(sh) != int(H):
                    surf = pygame.transform.smoothscale(surf, (int(W), int(H)))
                self._preview_cache = surf
                self._preview_cache_size = (int(W), int(H))
                return surf
            except Exception:
                pass

        try:
            sw, sh, rgb = self._g.screenshot_rgb()
            surf = pygame.image.frombuffer(rgb, (int(sw), int(sh)), "RGB")
            surf = surf.copy()
            if int(sw) != int(W) or int(sh) != int(H):
                surf = pygame.transform.smoothscale(surf, (int(W), int(H)))
            self._preview_cache = surf
            self._preview_cache_size = (int(W), int(H))
            return surf
        except Exception:
            return None
