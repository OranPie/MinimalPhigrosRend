from __future__ import annotations

from ._compat import core
from .analysis import rows_to_numpy, rows_to_pandas
from .eval import FrameEvaluator


class Chart:
    def __init__(self, native: core.ChartHandle):
        self._native = native

    @property
    def native(self) -> core.ChartHandle:
        return self._native

    @property
    def offset(self) -> float:
        return self._native.offset

    @property
    def chart_end(self) -> float:
        return self._native.chart_end

    @property
    def playable_count(self) -> int:
        return self._native.playable_count

    @property
    def notes_count(self) -> int:
        return self._native.notes_count

    @property
    def lines_count(self) -> int:
        return self._native.lines_count

    @property
    def config(self):
        return self._native.config

    def frame(self, t: float, config=None):
        cfg = None if config is None else getattr(config, "native", config)
        return self._native.build_frame(t, cfg)

    def frames(self, times, config=None):
        cfg = None if config is None else getattr(config, "native", config)
        return self._native.frames(list(times), cfg)

    def evaluator(self, mode: str = "aggressive", max_pointers: int = 2) -> FrameEvaluator:
        return FrameEvaluator(self, mode=mode, max_pointers=max_pointers)

    def compile(self, sample_rate: float = 240.0):
        return self._native.compile(sample_rate)

    def notes_data(self) -> list[dict]:
        return list(self._native.notes_data())

    def lines_data(self) -> list[dict]:
        return list(self._native.lines_data())

    def notes_numpy(self) -> dict:
        return rows_to_numpy(self.notes_data())

    def lines_numpy(self) -> dict:
        return rows_to_numpy(self.lines_data())

    def notes_pandas(self):
        return rows_to_pandas(self.notes_data())

    def lines_pandas(self):
        return rows_to_pandas(self.lines_data())

    def to_dict(self, include_notes: bool = False, include_lines: bool = False) -> dict:
        return self._native.to_dict(include_notes=include_notes, include_lines=include_lines)


def load_chart(path: str, width: int = 1280, height: int = 720,
               easing_shift: int = 0, password: str = "") -> Chart:
    return Chart(core.load_chart(path, width=width, height=height,
                                 easing_shift=easing_shift, password=password))


__all__ = ["Chart", "load_chart"]
