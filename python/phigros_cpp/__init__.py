from __future__ import annotations

from .analysis import rows_to_numpy, rows_to_pandas
from .chart import Chart, load_chart
from .config import LineAlphaMode, RenderConfig, config_from_dict, load_config
from .eval import AutoplayRun, FrameEvaluator, simulate_autoplay
from .io import (
    ChartAssets,
    ChartEntry,
    CompiledChart,
    CompressionAlgo,
    EncryptionAlgo,
    PhbcWriteOptions,
    compile_chart,
    read_phbc,
    scan_charts_directory,
    write_phbc,
)
from ._compat import core as _core

FrameSnapshot = _core.FrameSnapshot
HitEvent = _core.HitEvent
HudState = _core.HudState
LineSnapshot = _core.LineSnapshot
NoteSnapshot = _core.NoteSnapshot
ScoreResult = _core.ScoreResult
compute_score = _core.compute_score

# Compatibility aliases for callers still expecting the old native names.
ChartHandle = _core.ChartHandle
AutoplayResult = _core.AutoplayResult

__all__ = [
    "AutoplayResult",
    "AutoplayRun",
    "Chart",
    "ChartAssets",
    "ChartEntry",
    "ChartHandle",
    "CompiledChart",
    "CompressionAlgo",
    "EncryptionAlgo",
    "FrameEvaluator",
    "FrameSnapshot",
    "HitEvent",
    "HudState",
    "LineAlphaMode",
    "LineSnapshot",
    "NoteSnapshot",
    "PhbcWriteOptions",
    "RenderConfig",
    "ScoreResult",
    "compile_chart",
    "compute_score",
    "config_from_dict",
    "load_chart",
    "load_config",
    "read_phbc",
    "rows_to_numpy",
    "rows_to_pandas",
    "scan_charts_directory",
    "simulate_autoplay",
    "write_phbc",
]
