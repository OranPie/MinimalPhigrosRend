# MinimalPhigrosRend

> 🌐 [简体中文](README.zh.md)

MinimalPhigrosRend is a Phigros chart renderer and chart-processing toolkit built around a modern C++ core.

The active runtime surfaces in this repository are:

- `phigros_render`: native C++ renderer / player for desktop and WebAssembly
- `phigros_cpp`: Python bindings for chart loading, preprocessing, frame evaluation, autoplay simulation, and PHBC workflows
- helper scripts for local preview, chart scanning, ChartScript playback, and Web UI export

## Start Here

- Want to build and run the renderer: [docs/CPP_RENDERER.md](docs/CPP_RENDERER.md)
- Want to use the Python bindings: [docs/PYTHON_BINDINGS.md](docs/PYTHON_BINDINGS.md)
- Want the full documentation map: [docs/INDEX.md](docs/INDEX.md)
- Want C++ internal architecture/module docs: [cpp/docs/ARCHITECTURE.md](cpp/docs/ARCHITECTURE.md)

## Repository Structure

```text
MinimalPhigrosRend/
├── README.md / README.zh.md
├── docs/                 User-facing docs and navigation
├── config/               Example config files
├── assets/               Shared assets such as fonts
├── scripts/              Root helper scripts and local tooling
├── python/               Python package source for phigros_cpp
├── cpp/
│   ├── README.md         C++ renderer quickstart
│   ├── CMakeLists.txt    Native build targets
│   ├── include/phigros/  Public/native headers by subsystem
│   ├── src/              Native implementation
│   ├── docs/             Internal C++ architecture and subsystem docs
│   ├── tests/            Native test executables
│   ├── scripts/          Platform/build helper scripts
│   ├── shaders/          bgfx shader sources
│   ├── mods/             Built-in mod examples
│   ├── web/              Web shell assets
│   ├── android/          Android wrapper project
│   └── ios/              iOS wrapper project
└── respack.zip           Default resource pack
```

## Quick Start

Build the native renderer:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

Run a chart:

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

Build orchestration and launch helpers:

```bash
python3 scripts/build.py
python3 scripts/build.py --profile desktop --build-type Debug
python3 scripts/build.py --profile web --print-only
python3 -m phigros_ui              # unified launcher UI (PySide6)
python3 scripts/qt_launcher.py     # legacy alias for `python3 -m phigros_ui`
```

## Common Paths

- Renderer quickstart: [cpp/README.md](cpp/README.md)
- Renderer reference: [docs/CPP_RENDERER.md](docs/CPP_RENDERER.md)
- Python bindings reference: [docs/PYTHON_BINDINGS.md](docs/PYTHON_BINDINGS.md)
- Config usage: [docs/CONFIG_USAGE.md](docs/CONFIG_USAGE.md)
- ChartScript reference: [docs/CHARTSCRIPT.md](docs/CHARTSCRIPT.md)
- Internal C++ docs index: [cpp/docs/ARCHITECTURE.md](cpp/docs/ARCHITECTURE.md)

## Notes

- Root `docs/` is user-facing.
- `cpp/docs/` is for internal C++ module, data, math, format, render, and build documentation.
- Some local workflows expect a `charts/` directory if you want parser auto-discovery tests or manual sample runs.
