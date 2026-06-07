# MinimalPhigrosRend — C++ Renderer

> 🌐 [中文](README.zh.md)

This directory contains the native C++ implementation: parser/core library targets, the SDL renderer/player app, tests, platform wrappers, shaders, and internal subsystem docs.

## Directory Structure

```text
cpp/
├── CMakeLists.txt
├── README.md / README.zh.md
├── cmake/                Mobile/platform helper CMake files
├── include/phigros/
│   ├── api/              Native API exposed to Python bindings
│   ├── app/              SDL app, CLI, windowing, input, game-loop integration
│   ├── chart/            Chart loading, parsing, compilation, PHBC I/O
│   ├── config/           RenderConfig load/save and defaults
│   ├── core/             Core types, logging, mods
│   ├── engine/           Judge, autoplay, hold logic, kinematics, visibility
│   ├── hud/              HUD state
│   ├── io/               Audio, replay, respack, video encoder
│   ├── math/             Easing, tracks, utility math
│   └── render/           Frame building and draw backends
├── src/
│   ├── app/              SDL app and legacy CLI entries
│   ├── api/              C++ API implementation used by bindings
│   ├── chart/            Parser/loader/compiler implementations
│   ├── python/           Python extension module glue
│   ├── vendor/           Vendor wrapper translation units
│   └── main.cpp          Headless/native core entry
├── docs/                 Internal C++ documentation set
├── tests/                Native tests and benchmarks
├── scripts/              Build and helper scripts
├── shaders/              bgfx shader sources
├── mods/                 Built-in mod JSON examples
├── vendor/               Vendored libraries
├── web/                  WASM shell assets
├── android/              Android wrapper project
└── ios/                  iOS wrapper project
```

## Build

Desktop build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build build --target phigros_sdl_app --parallel
```

Common outputs:

- `build/phigros_sdl_app`
- `build/phigros_core`
- `build/chart_scanner`
- test binaries such as `build/test_engine`

The SDL app starts directly at the chart library. Put chart folders or zips under `charts/`; argv-based chart launching lives in the legacy CLI target and requires `-DBUILD_LEGACY_CLI=ON`.

## Use Paths

- Renderer app usage: [../docs/CPP_RENDERER.md](../docs/CPP_RENDERER.md)
- Python bindings usage: [../docs/PYTHON_BINDINGS.md](../docs/PYTHON_BINDINGS.md)
- Internal architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Interfaces: [docs/INTERFACES.md](docs/INTERFACES.md)
- Data structures: [docs/DATA_STRUCTURES.md](docs/DATA_STRUCTURES.md)
- Math: [docs/MATH.md](docs/MATH.md)
- Format internals: [docs/FORMAT.md](docs/FORMAT.md)
- Kinematics: [docs/KINEMATICS.md](docs/KINEMATICS.md)
- Render pipeline: [docs/RENDER.md](docs/RENDER.md)
- Config internals: [docs/CONFIG.md](docs/CONFIG.md)
- Build and tests: [docs/BUILD_AND_TEST.md](docs/BUILD_AND_TEST.md)

## Notes

- Root `docs/` documents user workflows.
- `cpp/docs/` documents C++ internals and is the canonical place for architecture/module references.
