# MinimalPhigrosRend

> 🌐 [简体中文](README.zh.md)

MinimalPhigrosRend is a Phigros chart renderer and chart-processing toolkit centered on a modern C++ core.

This repository currently contains:

- `phigros_render`: the native C++ renderer/app for desktop and WebAssembly
- `phigros_cpp`: Python bindings for chart loading, preprocessing, autoplay simulation, and PHBC workflows
- helper tools for local preview, web UI export, chart scanning, and mod/chartscript workflows

If you want one starting point for "how do I build this?" and "how do I use this?", this is it. The deeper references are linked below.

## Repository Layout

- [`cpp/`](/Users/yanyige/MinimalPhigrosRend/cpp): C++ source, CMake project, tests, platform wrappers
- [`docs/`](/Users/yanyige/MinimalPhigrosRend/docs): user/developer references for renderer, chartscript, Python bindings
- [`config/`](/Users/yanyige/MinimalPhigrosRend/config): example config files
- [`scripts/`](/Users/yanyige/MinimalPhigrosRend/scripts): helper scripts such as WASM serving and local web UI
- [`python/`](/Users/yanyige/MinimalPhigrosRend/python): Python package source for `phigros_cpp`
- [`charts/`](/Users/yanyige/MinimalPhigrosRend/charts): local chart library used for testing and manual runs if present

## What To Use

- Use `phigros_render` if you want rendering, interactive play, screenshots, recording, replay, or WASM builds.
- Use `phigros_cpp` if you want Python access to chart parsing, frame evaluation, autoplay simulation, or PHBC compile/read/write.
- Use ChartScript if you want scripted playlists or multi-chart batch playback.

## Prerequisites

Minimum local requirements:

- CMake 3.16+
- a C++17 compiler
- Python 3.10+ for bindings and helper scripts

Notes:

- Desktop builds fetch some dependencies with CMake `FetchContent` when they are not installed locally.
- `USE_SDL3=ON` fetches SDL3 for native app builds.
- `USE_SDL3=OFF` uses SDL2 on desktop and Emscripten's SDL2 port on web builds.
- Video export may use system libav if found, otherwise it falls back to spawning `ffmpeg`.

## Quick Start

### 1. Build the native renderer

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

Binary output:

- `cpp/build/phigros_render`

### 2. Run a chart

```bash
./cpp/build/phigros_render charts/MyChart/IN.json
```

Useful variants:

```bash
./cpp/build/phigros_render charts/MyChart/IN.json --score-only
./cpp/build/phigros_render charts/MyChart/IN.json --play
./cpp/build/phigros_render charts/MyChart/IN.json --record out.mp4
./cpp/build/phigros_render charts/MyChart/IN.json --config config/config.jsonc
```

### 3. Run tests

```bash
./cpp/build/test_easing
./cpp/build/test_engine
./cpp/build/test_parser charts
```

If your clone does not include [`charts/`](/Users/yanyige/MinimalPhigrosRend/charts), parser auto-discovery coverage will be limited.

## Development Workflows

### Native renderer development

Debug-style local build:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

Useful CMake options:

- `-DBUILD_RENDER_APP=ON|OFF`: build the native renderer executable
- `-DBUILD_PYTHON_BINDINGS=ON|OFF`: build Python extension targets
- `-DUSE_SDL3=ON|OFF`: SDL3 on native desktop, SDL2 fallback otherwise
- `-DUSE_BGFX=ON|OFF`: enable bgfx backend work
- `-DUSE_LIBAV=ON|OFF`: use FFmpeg dev libraries when available
- `-DUSE_LZMA=ON|OFF`: enable PHBC LZMA compression support
- `-DUSE_ENCRYPTION=ON|OFF`: enable OpenSSL-backed PHBC encryption
- `-DUSE_SANITIZERS=ON|OFF`: ASan/UBSan for development builds

### Python bindings development

Wheel-style build from repo root:

```bash
python3 -m pip install -U pip build
python3 -m build
```

Direct CMake build:

```bash
cmake -S cpp -B cpp/build_py \
  -DBUILD_PYTHON_BINDINGS=ON \
  -DBUILD_RENDER_APP=OFF \
  -DUSE_BGFX=OFF \
  -DUSE_LIBAV=OFF
cmake --build cpp/build_py --target _core --parallel
```

Load from the repo checkout:

```bash
PYTHONPATH=python:cpp/build_py python3
```

Minimal smoke test:

```python
import phigros_cpp as pc
chart = pc.load_chart("charts/MyChart/IN.json")
print(chart.playable_count, chart.chart_end)
```

### WASM build

```bash
cd cpp
./scripts/build_web.sh Release
cd ..
python3 scripts/serve.py --dir cpp/build_web
```

This requires an active Emscripten environment. The helper script uses `emcmake` and `emmake` for you.

## Usage Workflows

### Renderer CLI

Common tasks:

- autoplay/headless scoring: `--score-only`
- interactive play: `--play`
- record MP4/video: `--record out.mp4`
- replay save/load: `--save-replay` and `--play-replay`
- benchmark: `--benchmark --benchmark-iterations N`
- scripted playlists: `--script file.chartscript.json`
- compile chart to PHBC: `--compile out.phbc`

Detailed CLI and config reference:

- [`docs/CPP_RENDERER.md`](/Users/yanyige/MinimalPhigrosRend/docs/CPP_RENDERER.md)
- [`cpp/README.md`](/Users/yanyige/MinimalPhigrosRend/cpp/README.md)

### Config files

The shared example config lives at:

- [`config/config.jsonc`](/Users/yanyige/MinimalPhigrosRend/config/config.jsonc)

Renderer config references:

- [`cpp/docs/CONFIG.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/CONFIG.md)
- [`docs/CPP_RENDERER.md`](/Users/yanyige/MinimalPhigrosRend/docs/CPP_RENDERER.md)

### Python API usage

Primary reference:

- [`docs/PYTHON_BINDINGS.md`](/Users/yanyige/MinimalPhigrosRend/docs/PYTHON_BINDINGS.md)

### Local helper tools

- TUI launcher: `python3 cpp/scripts/launcher.py`
- WebAssembly preview server: `python3 scripts/serve.py`
- Browser-based local web UI for preview/export: `python3 scripts/webui.py`

Helper script dependencies:

- `cpp/scripts/launcher.py` requires `textual`
- `scripts/webui.py` requires `flask`

Script details:

- [`cpp/scripts/launcher.py`](/Users/yanyige/MinimalPhigrosRend/cpp/scripts/launcher.py)
- [`scripts/serve.py`](/Users/yanyige/MinimalPhigrosRend/scripts/serve.py)
- [`scripts/webui.py`](/Users/yanyige/MinimalPhigrosRend/scripts/webui.py)

## Documentation Map

- Native renderer quickstart: [`cpp/README.md`](/Users/yanyige/MinimalPhigrosRend/cpp/README.md)
- Native renderer full reference: [`docs/CPP_RENDERER.md`](/Users/yanyige/MinimalPhigrosRend/docs/CPP_RENDERER.md)
- ChartScript reference: [`docs/CHARTSCRIPT.md`](/Users/yanyige/MinimalPhigrosRend/docs/CHARTSCRIPT.md)
- Python bindings: [`docs/PYTHON_BINDINGS.md`](/Users/yanyige/MinimalPhigrosRend/docs/PYTHON_BINDINGS.md)
- Core architecture: [`cpp/docs/ARCHITECTURE.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/ARCHITECTURE.md)
- Config reference: [`cpp/docs/CONFIG.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/CONFIG.md)
- Debug flags: [`cpp/docs/DEBUG_FLAGS.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/DEBUG_FLAGS.md)
- Chart loader internals: [`cpp/docs/CHART_LOADER.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/CHART_LOADER.md)

## Current Status Notes

- The active, documented runtime paths in this repository are the C++ renderer and the `phigros_cpp` bindings.
- Some older docs still refer to the historical Python `phic_renderer` workflow. Treat those as legacy unless that code is restored in this checkout.
