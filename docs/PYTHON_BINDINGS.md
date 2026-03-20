# Python Bindings

The `phigros_cpp` package exposes the C++ chart-processing pipeline to Python.
It is intended for chart loading, preprocessing, evaluation, autoplay simulation,
and PHBC compile/read/write workflows. It does not expose the SDL windowing or
texture/render backend.

## Scope

`phigros_cpp` currently supports:

- loading Official, RPE, PEC, folder, zip-ref, and `.phbc` charts
- chart discovery with `scan_charts_directory()`
- config loading with `load_config()` and `config_from_dict()`
- frame evaluation with `ChartHandle.build_frame()` / `ChartHandle.frames()`
- autoplay simulation with `simulate_autoplay()`
- score computation with `compute_score()`
- PHBC compile/read/write helpers

Not included:

- `phigros_render` SDL app/window APIs
- respack loading
- texture / draw-call access
- video export bindings

## Build

### Wheel-style build

From the repo root:

```bash
python3.10 -m pip install -U pip build
python3.10 -m build
```

This uses [pyproject.toml](/Users/yanyige/MinimalPhigrosRend/pyproject.toml) and
builds the extension from `cpp/` through `scikit-build-core`.

### Development build

If you want to build the extension directly with CMake:

```bash
cd cpp
cmake -S . -B build_py \
  -DBUILD_PYTHON_BINDINGS=ON \
  -DBUILD_RENDER_APP=OFF \
  -DUSE_LIBAV=OFF \
  -DUSE_BGFX=OFF
cmake --build build_py --target _core -j4
```

Then import it from the repo root with:

```bash
PYTHONPATH=python:cpp/build_py python3.10
```

## Quick Start

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json", width=1280, height=720)
frame = chart.build_frame(12.5)

print(chart.notes_count, chart.playable_count)
print(frame.hud.score, len(frame.notes))

result = pc.simulate_autoplay(chart, fps=240.0, mode="aggressive")
print(result.score.score, result.max_combo)
```

## Main API

### Top-level functions

- `load_chart(path, width=1280, height=720, easing_shift=0, password="") -> ChartHandle`
- `scan_charts_directory(path) -> list[ChartEntry]`
- `load_config(path) -> RenderConfig`
- `config_from_dict(data) -> RenderConfig`
- `compute_score(acc_sum, max_combo, total_notes) -> ScoreResult`
- `compile_chart(chart, sample_rate=240.0) -> CompiledChart`
- `read_phbc(path, password="") -> CompiledChart`
- `write_phbc(compiled, path, options=PhbcWriteOptions()) -> None`
- `simulate_autoplay(chart, fps=240.0, mode="aggressive", max_pointers=2, duration=None) -> AutoplayResult`

### `ChartHandle`

Read-only metadata:

- `offset`
- `chart_end`
- `playable_count`
- `notes_count`
- `lines_count`
- `config`

Methods:

- `build_frame(t, config=None) -> FrameSnapshot`
- `frames(times, config=None) -> list[FrameSnapshot]`
- `compile(sample_rate=240.0) -> CompiledChart`
- `to_dict(include_notes=False, include_lines=False) -> dict`

### `RenderConfig`

Useful evaluation fields exposed by the binding:

- `window_w`, `window_h`
- `expand_factor`
- `note_scale_x`, `note_scale_y`
- `note_flow_speed_multiplier`
- `note_speed_mul_affects_travel`
- `note_alpha`
- `approach`, `chart_speed`
- `no_cull`, `no_cull_screen`, `no_cull_enter_time`
- `overrender`
- `line_alpha_mode`
- `rpe_easing_shift`

### `FrameSnapshot`

- `t`
- `lines`
- `notes`
- `hud`
- `to_dict()`

Each line/note snapshot is exposed as a typed object and also supports `to_dict()`.

## PHBC Example

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json")
compiled = chart.compile(sample_rate=240.0)

opts = pc.PhbcWriteOptions()
opts.compress = True
opts.compress_algo = pc.CompressionAlgo.ZLIB

pc.write_phbc(compiled, "out.phbc", opts)
compiled2 = pc.read_phbc("out.phbc")
chart2 = compiled2.to_chart()
```

## Notes

- `ChartHandle` is preprocessed when loaded. If you need a different width,
  height, or preprocessing-sensitive config, load a new chart handle.
- `build_frame()` is evaluation-oriented. It returns CPU-side snapshots, not
  pixels or draw commands.
- `simulate_autoplay()` uses the same core engine path as the native app’s
  score-only/autoplay flow.
