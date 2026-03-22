from __future__ import annotations

from ._compat import core
from .analysis import rows_to_numpy, rows_to_pandas


class AutoplayRun:
    def __init__(self, native: core.AutoplayResult):
        self._native = native

    @property
    def native(self) -> core.AutoplayResult:
        return self._native

    @property
    def score(self):
        return self._native.score

    @property
    def judged_count(self) -> int:
        return self._native.judged_count

    @property
    def playable_count(self) -> int:
        return self._native.playable_count

    @property
    def max_combo(self) -> int:
        return self._native.max_combo

    @property
    def hit_events(self):
        return self._native.hit_events

    def to_dict(self) -> dict:
        return self._native.to_dict()

    def hit_events_data(self) -> list[dict]:
        return [event.to_dict() for event in self._native.hit_events]

    def hit_events_numpy(self) -> dict:
        return rows_to_numpy(self.hit_events_data())

    def hit_events_pandas(self):
        return rows_to_pandas(self.hit_events_data())


class FrameEvaluator:
    def __init__(self, chart, mode: str = "aggressive", max_pointers: int = 2):
        self._chart = chart
        self._native = core.FrameEvaluator(chart.native, mode, max_pointers)

    @property
    def native(self) -> core.FrameEvaluator:
        return self._native

    @property
    def sim_t(self) -> float:
        return self._native.sim_t

    def reset(self) -> None:
        self._native.reset()

    def build_frame(self, t: float, config=None):
        cfg = None if config is None else getattr(config, "native", config)
        return self._native.build_frame(t, cfg)

    def build_frames(self, times, config=None):
        cfg = None if config is None else getattr(config, "native", config)
        return self._native.build_frames(list(times), cfg)


def simulate_autoplay(chart, fps: float = 240.0, mode: str = "aggressive",
                      max_pointers: int = 2, duration=None) -> AutoplayRun:
    native = core.simulate_autoplay(chart.native, fps=fps, mode=mode,
                                    max_pointers=max_pointers, duration=duration)
    return AutoplayRun(native)


__all__ = ["AutoplayRun", "FrameEvaluator", "simulate_autoplay"]
