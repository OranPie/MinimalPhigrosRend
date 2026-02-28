from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Optional, Tuple


class TransitionPhase(str, Enum):
    INTRO_LOADING = "intro_loading"
    INTRO_LINE_OPEN = "intro_line_open"
    GAMEPLAY = "gameplay"
    OUTRO_SETTLEMENT = "outro_settlement"
    OUTRO_LINE_CLOSE = "outro_line_close"
    DONE = "done"


@dataclass(frozen=True)
class TransitionDurations:
    loading_sec: float = 0.85
    line_open_sec: float = 0.60
    settlement_sec: float = 1.25
    line_close_sec: float = 0.65

    @property
    def intro_total_sec(self) -> float:
        return float(self.loading_sec) + float(self.line_open_sec)

    @property
    def outro_total_sec(self) -> float:
        return float(self.settlement_sec) + float(self.line_close_sec)


@dataclass
class TransitionFrame:
    phase: TransitionPhase
    phase_t: float
    phase_progress: float
    chart_time: float
    chart_time_frozen: bool
    should_start_audio: bool
    should_stop_audio: bool


def _clamp01(x: float) -> float:
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    return float(x)


def _progress(t: float, dur: float) -> float:
    if dur <= 1e-9:
        return 1.0
    return _clamp01(float(t) / float(dur))


class TransitionController:
    def __init__(
        self,
        *,
        durations: Optional[TransitionDurations] = None,
        intro_freeze_time: float = 0.0,
    ):
        self.durations = durations or TransitionDurations()
        self.phase: TransitionPhase = TransitionPhase.INTRO_LOADING
        self.phase_t: float = 0.0
        self._audio_started: bool = False
        self._audio_stopped: bool = False
        self.intro_freeze_time: float = float(intro_freeze_time)

    def reset(self) -> None:
        self.phase = TransitionPhase.INTRO_LOADING
        self.phase_t = 0.0
        self._audio_started = False
        self._audio_stopped = False

    def in_intro(self) -> bool:
        return self.phase in (TransitionPhase.INTRO_LOADING, TransitionPhase.INTRO_LINE_OPEN)

    def in_outro(self) -> bool:
        return self.phase in (TransitionPhase.OUTRO_SETTLEMENT, TransitionPhase.OUTRO_LINE_CLOSE)

    def is_done(self) -> bool:
        return self.phase == TransitionPhase.DONE

    def update(
        self,
        *,
        dt_present: float,
        chart_time: float,
        chart_end: Optional[float],
        request_outro: bool,
    ) -> TransitionFrame:
        dtp = float(dt_present)
        if dtp < 0.0:
            dtp = 0.0

        should_start_audio = False
        should_stop_audio = False

        if self.phase == TransitionPhase.INTRO_LOADING:
            self.phase_t += dtp
            if self.phase_t >= float(self.durations.loading_sec):
                self.phase_t -= float(self.durations.loading_sec)
                self.phase = TransitionPhase.INTRO_LINE_OPEN

        elif self.phase == TransitionPhase.INTRO_LINE_OPEN:
            self.phase_t += dtp
            if self.phase_t >= float(self.durations.line_open_sec):
                self.phase_t = 0.0
                self.phase = TransitionPhase.GAMEPLAY

        elif self.phase == TransitionPhase.GAMEPLAY:
            if request_outro:
                self.phase = TransitionPhase.OUTRO_SETTLEMENT
                self.phase_t = 0.0

        elif self.phase == TransitionPhase.OUTRO_SETTLEMENT:
            self.phase_t += dtp
            if self.phase_t >= float(self.durations.settlement_sec):
                self.phase_t -= float(self.durations.settlement_sec)
                self.phase = TransitionPhase.OUTRO_LINE_CLOSE

        elif self.phase == TransitionPhase.OUTRO_LINE_CLOSE:
            self.phase_t += dtp
            if self.phase_t >= float(self.durations.line_close_sec):
                self.phase = TransitionPhase.DONE
                self.phase_t = 0.0

        elif self.phase == TransitionPhase.DONE:
            self.phase_t = 0.0

        if self.phase == TransitionPhase.GAMEPLAY and (not self._audio_started):
            self._audio_started = True
            should_start_audio = True

        if self.in_outro() and (not self._audio_stopped):
            self._audio_stopped = True
            should_stop_audio = True

        chart_time_out = float(chart_time)
        frozen = False
        if self.in_intro():
            chart_time_out = float(self.intro_freeze_time)
            frozen = True
        elif self.in_outro():
            if chart_end is not None:
                try:
                    chart_time_out = float(chart_end)
                    frozen = True
                except Exception:
                    pass

        if self.phase == TransitionPhase.INTRO_LOADING:
            p = _progress(self.phase_t, float(self.durations.loading_sec))
        elif self.phase == TransitionPhase.INTRO_LINE_OPEN:
            p = _progress(self.phase_t, float(self.durations.line_open_sec))
        elif self.phase == TransitionPhase.OUTRO_SETTLEMENT:
            p = _progress(self.phase_t, float(self.durations.settlement_sec))
        elif self.phase == TransitionPhase.OUTRO_LINE_CLOSE:
            p = _progress(self.phase_t, float(self.durations.line_close_sec))
        else:
            p = 0.0

        return TransitionFrame(
            phase=self.phase,
            phase_t=float(self.phase_t),
            phase_progress=float(p),
            chart_time=float(chart_time_out),
            chart_time_frozen=bool(frozen),
            should_start_audio=bool(should_start_audio),
            should_stop_audio=bool(should_stop_audio),
        )


def map_presentation_time_to_chart_time(
    *,
    t_present: float,
    durations: Optional[TransitionDurations] = None,
    chart_end: Optional[float] = None,
    intro_freeze_time: float = 0.0,
) -> Tuple[float, TransitionPhase, float]:
    d = durations or TransitionDurations()
    tp = float(t_present)

    if tp < float(d.intro_total_sec):
        if tp < float(d.loading_sec):
            return float(intro_freeze_time), TransitionPhase.INTRO_LOADING, _progress(tp, float(d.loading_sec))
        return (
            float(intro_freeze_time),
            TransitionPhase.INTRO_LINE_OPEN,
            _progress(tp - float(d.loading_sec), float(d.line_open_sec)),
        )

    t_game = float(intro_freeze_time) + (tp - float(d.intro_total_sec))
    if chart_end is not None and t_game >= float(chart_end):
        t_after = t_game - float(chart_end)
        if t_after < float(d.settlement_sec):
            return float(chart_end), TransitionPhase.OUTRO_SETTLEMENT, _progress(t_after, float(d.settlement_sec))
        t_after2 = t_after - float(d.settlement_sec)
        if t_after2 < float(d.line_close_sec):
            return float(chart_end), TransitionPhase.OUTRO_LINE_CLOSE, _progress(t_after2, float(d.line_close_sec))
        return float(chart_end), TransitionPhase.DONE, 1.0

    return float(t_game), TransitionPhase.GAMEPLAY, 0.0
