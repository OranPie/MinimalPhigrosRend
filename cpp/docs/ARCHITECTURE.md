# Architecture Overview

> 🌐 [中文](ARCHITECTURE.zh.md)

This page documents the current C++ module layout and runtime flow.

For user workflows, start from [../../docs/CPP_RENDERER.md](../../docs/CPP_RENDERER.md). For subsystem detail, continue into the dedicated pages linked below.

## Directory Structure

```text
cpp/
├── CMakeLists.txt
├── include/phigros/
│   ├── api/      Native API surface used by Python bindings
│   ├── app/      CLI, window, input, platform integration
│   ├── chart/    Loaders, parsers, compiler, PHBC I/O
│   ├── config/   RenderConfig load/save/defaults
│   ├── core/     Core types, logging, mods
│   ├── engine/   Judge, autoplay, hold logic, kinematics, visibility
│   ├── hud/      HUD state
│   ├── io/       Audio, replay, respack, video encoder
│   ├── math/     Easing, tracks, math utilities
│   └── render/   Frame snapshots, renderers, executors, targets
├── src/
│   ├── api/      PreparedChart / autoplay / PHBC implementation
│   ├── app/      Native renderer executable entry
│   ├── chart/    Parser/loader/compiler implementations
│   ├── python/   Python extension module glue
│   ├── vendor/   Vendor-backed translation units
│   └── main.cpp  Headless/native core entry
├── tests/        Native tests and benchmark entrypoints
├── vendor/       miniz, miniaudio, stb
├── scripts/      Build helpers and launcher tooling
├── shaders/      bgfx shader sources
├── mods/         Built-in mod examples
├── web/          WASM shell assets
├── android/      Android wrapper project
└── ios/          iOS wrapper project
```

## Target Graph

Key targets defined from `cpp/CMakeLists.txt`:

- `phigros_core_lib`: core chart/parser/compiler library
- `phigros_core`: headless/native CLI target built on `phigros_core_lib`
- `phigros_python_api_lib`: native API layer used by the Python extension
- `chart_scanner`: chart discovery utility
- `test_easing`, `test_engine`, `test_parser`, `test_logger`, `test_zip_extract`, `verify_chart`, `bench`
- `phigros_render`: renderer/player app when render-app targets are enabled

Vendor support libraries include `vendor_stb`, `vendor_miniz`, and `vendor_miniaudio`.

## Runtime Data Flow

```text
CLI args / Python call / script item
        │
        ▼
RenderConfig + path resolution
        │
        ▼
chart::load / parse / read_phbc
        │
        ▼
ChartData or CompiledChartData
        │
        ▼
PreparedChart / runtime state
        │
        ├── engine::SimulatePlayer
        ├── engine::ManualJudge
        ├── engine::Judge
        └── hold / visibility / effects helpers
                │
                ▼
render::build_frame()
                │
                ▼
FrameSnapshot { lines, notes, hud }
                │
                ▼
SDL/bgfx renderer path or Python consumer
```

## Module Responsibilities

- `core/`: canonical runtime structs such as `Note`, `Line`, `ChartData`, and note-state data.
- `chart/`: format-specific parsing, chart discovery, asset resolution, compiled-chart conversion, PHBC read/write.
- `math/`: easing and piecewise track evaluation shared by parsers, kinematics, compiler, and render.
- `engine/`: time-domain gameplay simulation and note/line evaluation helpers.
- `render/`: CPU frame-snapshot construction and backend-specific draw execution.
- `config/`: JSON-to-`RenderConfig` conversion, defaults, clamping, round-trip serialization.
- `api/`: stable native helper layer consumed by Python bindings.
- `app/`: executable integration layer, input loop, CLI, and platform-specific runtime wiring.
- `io/`: audio playback, replay persistence, respack handling, and video output.

## Internal Doc Map

- [INTERFACES.md](INTERFACES.md)
- [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
- [MATH.md](MATH.md)
- [FORMAT.md](FORMAT.md)
- [KINEMATICS.md](KINEMATICS.md)
- [RENDER.md](RENDER.md)
- [CONFIG.md](CONFIG.md)
- [BUILD_AND_TEST.md](BUILD_AND_TEST.md)
- [CHART_LOADER.md](CHART_LOADER.md)
- [DEBUG_FLAGS.md](DEBUG_FLAGS.md)
