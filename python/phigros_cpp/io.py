from __future__ import annotations

from ._compat import core

ChartAssets = core.ChartAssets
ChartEntry = core.ChartEntry
CompiledChart = core.CompiledChart
CompressionAlgo = core.CompressionAlgo
EncryptionAlgo = core.EncryptionAlgo
PhbcWriteOptions = core.PhbcWriteOptions
scan_charts_directory = core.scan_charts_directory


def compile_chart(chart, sample_rate: float = 240.0):
    native = getattr(chart, "native", chart)
    return core.compile_chart(native, sample_rate=sample_rate)


def read_phbc(path: str, password: str = ""):
    return core.read_phbc(path, password=password)


def write_phbc(compiled, path: str, options=None):
    if options is None:
        options = PhbcWriteOptions()
    return core.write_phbc(compiled, path, options=options)


__all__ = [
    "ChartAssets",
    "ChartEntry",
    "CompiledChart",
    "CompressionAlgo",
    "EncryptionAlgo",
    "PhbcWriteOptions",
    "compile_chart",
    "read_phbc",
    "scan_charts_directory",
    "write_phbc",
]
