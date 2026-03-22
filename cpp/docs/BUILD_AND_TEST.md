# Build And Test

> 🌐 [中文](BUILD_AND_TEST.zh.md)

This page documents the main build switches, target families, and verification workflow.

## Main Configure Flags

Important CMake options from `cpp/CMakeLists.txt`:

- `BUILD_PYTHON_BINDINGS`
- `BUILD_RENDER_APP`
- `USE_SDL3`
- `USE_BGFX`
- `USE_LIBAV`
- `USE_LZMA`
- `USE_ENCRYPTION`
- `USE_SANITIZERS`

These options decide which runtime surfaces and optional dependencies are compiled in.

## Common Build Paths

Renderer build:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

Bindings build:

```bash
cmake -S cpp -B cpp/build_py -DBUILD_PYTHON_BINDINGS=ON -DBUILD_RENDER_APP=OFF -DUSE_BGFX=OFF -DUSE_LIBAV=OFF
cmake --build cpp/build_py --target _core --parallel
```

## Target Families

Core/runtime targets:

- `phigros_core_lib`
- `phigros_core`
- `phigros_python_api_lib`
- `phigros_render`
- `chart_scanner`

Test and benchmark targets:

- `test_easing`
- `test_engine`
- `test_parser`
- `test_logger`
- `test_zip_extract`
- `verify_chart`
- `bench`
- `run-tests` convenience target when the binaries exist

## Platform Notes

- Desktop: SDL3 by default when enabled; SDL2 fallback paths exist.
- WebAssembly: uses the web build helpers and Emscripten environment.
- Mobile: platform wrappers live under `android/`, `ios/`, and `cmake/mobile.cmake`.
- Optional codec/compression/encryption support depends on system packages and build flags.

## Verification Workflow

Typical native checks:

```bash
./cpp/build/test_easing
./cpp/build/test_engine
./cpp/build/test_parser charts
```

Useful documentation-adjacent checks:

- verify CLI flags against `app_args.hpp`
- verify config fields against `render_config.hpp`
- verify target names against `cpp/CMakeLists.txt`
- verify module trees against `cpp/include/phigros` and `cpp/src`

## Doc Maintenance Rule

Update docs when any of these change:

- public CLI flags
- `RenderConfig` fields or defaults
- core chart/runtime structures
- chart or PHBC format behavior
- target names or major build flags
- backend/platform support boundaries
