# Python Bindings

> 🌐 [中文](PYTHON_BINDINGS.zh.md)

`phigros_cpp` exposes the C++ chart-processing pipeline to Python.

It is intended for chart loading, preprocessing, repeated frame evaluation, autoplay simulation, data analysis, and PHBC compile/read/write workflows. It does not expose the SDL windowing layer or native render backend objects.

## Build

Wheel-style build from the repository root:

```bash
python3 -m pip install -U pip build
python3 -m build
```

Optional analysis extras:

```bash
python3 -m pip install ".[analysis]"
```

Direct CMake build:

```bash
cmake -S cpp -B cpp/build_py -DBUILD_PYTHON_BINDINGS=ON -DBUILD_RENDER_APP=OFF -DUSE_LIBAV=OFF -DUSE_BGFX=OFF
cmake --build cpp/build_py --target _core --parallel
```

Import from the checkout:

```bash
PYTHONPATH=python:cpp/build_py python3
```

## Quick Start

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json", width=1280, height=720)
frame = chart.frame(12.5)
result = pc.simulate_autoplay(chart, fps=240.0, mode="aggressive")
evaluator = chart.evaluator()
frames = evaluator.build_frames([0.0, 0.5, 1.0])

print(chart.playable_count, frame.hud.score, result.score.score, len(frames))
```

## Main API Surface

Top-level helpers:

- `load_chart()`
- `scan_charts_directory()`
- `load_config()`
- `config_from_dict()`
- `compute_score()`
- `compile_chart()`
- `read_phbc()` / `write_phbc()`
- `simulate_autoplay()`
- `rows_to_numpy()` / `rows_to_pandas()`

Primary objects:

- `Chart`
- `FrameEvaluator`
- `AutoplayRun`
- `RenderConfig`
- `FrameSnapshot`
- `CompiledChart`
- `PhbcWriteOptions`

Common analysis helpers:

- `chart.notes_data()` / `chart.lines_data()`
- `chart.notes_numpy()` / `chart.notes_pandas()`
- `result.hit_events_data()` / `result.hit_events_numpy()` / `result.hit_events_pandas()`

## Scope Boundaries

Included:

- chart parsing and discovery
- CPU-side frame snapshots
- autoplay simulation results
- config loading and conversion
- PHBC workflows

Not included:

- SDL app/window APIs
- texture or draw-call access
- respack loading interfaces
- video export bindings

## Related Docs

- User config workflow: [CONFIG_USAGE.md](CONFIG_USAGE.md)
- Renderer usage: [CPP_RENDERER.md](CPP_RENDERER.md)
- Internal interfaces: [../cpp/docs/INTERFACES.md](../cpp/docs/INTERFACES.md)
- Data structures: [../cpp/docs/DATA_STRUCTURES.md](../cpp/docs/DATA_STRUCTURES.md)
- Format internals: [../cpp/docs/FORMAT.md](../cpp/docs/FORMAT.md)
- Config internals: [../cpp/docs/CONFIG.md](../cpp/docs/CONFIG.md)
