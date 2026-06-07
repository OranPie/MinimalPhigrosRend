# Interfaces

> 🌐 [中文](INTERFACES.zh.md)

This page separates user-facing, binding-facing, and internal-only interfaces.

## User-Facing Entry Surfaces

- `phigros_sdl_app`: native SDL renderer/player app
- `phigros_render`: legacy native renderer/player CLI
- `phigros_cpp`: Python package built from the native core
- `chart_scanner`: chart discovery utility binary

These are the surfaces external users should depend on first.

## Native Header Interfaces

Primary native interfaces under `include/phigros/`:

- `chart/`: chart loading, format parsing, compilation, PHBC read/write
- `config/render_config.hpp`: `RenderConfig` and config conversion helpers
- `core/types.hpp`: canonical runtime structs
- `engine/`: judge logic, autoplay, visibility, kinematics, hold logic
- `render/renderer.hpp`: CPU-side `FrameSnapshot` builder
- `api/python_api.hpp`: prepared-chart and autoplay helpers exposed through the Python binding layer

## Executable and Module Entry Points

- `src/app/sdl_mobile_app.cpp`: SDL app entry shared by desktop, Android, and iOS
- `src/app/main.cpp`: legacy renderer/player CLI entry
- `src/main.cpp`: headless/native core entry
- `src/python/module.cpp`: Python extension module definition
- `src/api/python_api.cpp`: native API implementation shared by bindings
- `include/phigros/api/mobile_bridge.h`: legacy C ABI used by old native mobile shells
- `include/phigros/app/app_args.hpp`: CLI option surface definition

## Binding Boundary

The Python package is intentionally narrower than the native renderer app:

- exposed: chart loading, config loading, frame evaluation, autoplay, PHBC workflows
- not exposed: SDL windowing, texture ownership, backend executors, app/game-loop integration

`PreparedChart`, `FrameEvaluator`, autoplay helpers, and PHBC helpers are the main concepts on the native side of the binding boundary.

## Internal-Only Interfaces

Treat these as implementation detail unless a page explicitly documents them as public contracts:

- `app/` integration classes such as window, input manager, and platform wiring
- backend executors and renderer-specific draw helpers in `render/`
- vendor wrapper translation units in `src/vendor/`
- mobile wrapper projects under `android/` and `ios/`

## Related Docs

- [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
- [FORMAT.md](FORMAT.md)
- [RENDER.md](RENDER.md)
- [../../docs/PYTHON_BINDINGS.md](../../docs/PYTHON_BINDINGS.md)
