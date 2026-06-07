# MinimalPhigrosRend

> 🌐 [简体中文](README.zh.md)

MinimalPhigrosRend is a Phigros chart renderer and chart-processing toolkit built around a modern C++ core.

The active runtime surfaces in this repository are:

- `phigros_sdl_app`: native SDL app for desktop, Android, and iOS
- `phigros_render`: optional legacy CLI renderer/player when `BUILD_LEGACY_CLI=ON`
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

Build the native SDL app:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --target phigros_sdl_app --parallel
```

Launch the app:

```bash
./cpp/build/phigros_sdl_app
```

The app opens directly into the SDL chart library. Put chart folders or zips under `charts/` and use the in-app library, detail, settings, play, pause, and result screens.

Legacy CLI tools are still available when explicitly enabled:

```bash
cmake -S cpp -B cpp/build_cli -DBUILD_LEGACY_CLI=ON -DUSE_BGFX=OFF
cmake --build cpp/build_cli --target phigros_render --parallel
./cpp/build_cli/phigros_render charts/MyChart/IN.json --score-only
```

Build orchestration helpers:

```bash
python3 scripts/build.py
python3 scripts/build.py --profile desktop --build-type Debug
python3 scripts/build.py --profile web --print-only
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
