# Python Bindings

> 🌐 [中文](PYTHON_BINDINGS.zh.md)

`phigros_cpp` exposes the C++ chart-processing pipeline to Python.

It is intended for chart loading, preprocessing, frame evaluation, autoplay simulation, and PHBC compile/read/write workflows. It does not expose the SDL windowing layer or native render backend objects.

## Build

Wheel-style build from the repository root:

```bash
python3 -m pip install -U pip build
python3 -m build
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
frame = chart.build_frame(12.5)
result = pc.simulate_autoplay(chart, fps=240.0, mode="aggressive")

print(chart.playable_count, frame.hud.score, result.score.score)
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

Primary objects:

- `ChartHandle`
- `RenderConfig`
- `FrameSnapshot`
- `CompiledChart`
- `PhbcWriteOptions`

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
