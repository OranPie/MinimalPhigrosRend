# Copilot Instructions

MinimalPhigrosRend is a Phigros chart renderer and chart-processing toolkit built around a modern C++17 core with Python bindings.

## Build Commands

**Native renderer (desktop):**
```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

**Python bindings only:**
```bash
cmake -S cpp -B cpp/build_py -DBUILD_PYTHON_BINDINGS=ON -DBUILD_RENDER_APP=OFF -DUSE_BGFX=OFF -DUSE_LIBAV=OFF
cmake --build cpp/build_py --target _core --parallel
# Import from checkout: PYTHONPATH=python:cpp/build_py python3
```

**Python wheel (full package):**
```bash
python3 -m pip install -U pip build
python3 -m build
```

**Key CMake options:** `BUILD_PYTHON_BINDINGS`, `BUILD_RENDER_APP`, `USE_SDL3`, `USE_BGFX`, `USE_LIBAV`, `USE_LZMA`, `USE_ENCRYPTION`, `USE_SANITIZERS`

## Test Commands

Run individual test binaries after building:
```bash
./cpp/build/test_easing                        # easing math unit tests
./cpp/build/test_engine                        # engine unit tests (no charts)
./cpp/build/test_engine --auto-discover charts # autoplay + compile round-trip on real charts
./cpp/build/test_parser charts                 # parser tests against charts/ directory
./cpp/build/test_logger
./cpp/build/test_zip_extract
```

CI also runs `cmake --build ... --target run-tests` when test binaries are present.

## Architecture

The pipeline flows through these stages:

```
CLI args / Python call
  → RenderConfig + path resolution
  → chart::load / parse / read_phbc
  → ChartData or CompiledChartData
  → PreparedChart / runtime state
  → engine::SimulatePlayer / ManualJudge / Judge
  → render::build_frame()
  → FrameSnapshot { lines, notes, hud }
  → SDL/bgfx draw path  OR  Python consumer
```

**Module responsibilities (`cpp/include/phigros/`):**
- `core/` — canonical runtime structs: `Note`, `Line`, `ChartData`, note-state data
- `chart/` — format parsing (official/RPE/PEC), chart discovery, PHBC read/write, `ChartAssets`/`ChartEntry`
- `math/` — easing, piecewise `tracks.hpp` (time-domain evaluation), `IntegralTrack` for scroll, BPM mapping
- `engine/` — judge logic, autoplay, hold logic, kinematics, visibility
- `render/` — CPU `FrameSnapshot` construction; backend executors (`sdl_renderer`, `bgfx_renderer`)
- `config/` — JSON-to-`RenderConfig` conversion, defaults, clamping, round-trip serialization
- `api/` — stable `PreparedChart` / `FrameEvaluator` / PHBC helpers consumed by Python bindings
- `app/` — CLI args (`app_args.hpp`), input loop, game-loop wiring, debug flags
- `io/` — audio, replay, respack, video encoder

**CMake targets:** `phigros_core_lib`, `phigros_core`, `phigros_python_api_lib`, `phigros_render`, `chart_scanner`, plus test/benchmark binaries.

**Source entries:**
- `src/app/main.cpp` — renderer/player executable
- `src/main.cpp` — headless/native core entry
- `src/python/module.cpp` — Python extension module
- `src/api/python_api.cpp` — native API shared by bindings

## Key Conventions

### Data model invariants
- `ChartData.notes` is sorted by `t_hit` after parsing.
- `playable_count` counts `fake == false` notes only.
- `mh` (simultaneous/multi-hit flag) is assigned during `ChartData::finalize()`.
- `early_notes` and `notes_by_enter` are sorted by `t_enter` for visibility candidate selection.
- Compiled charts set `is_compiled = true`; callers skip `precompute_t_enter()` for these.
- `CompiledChartData::to_chart_data()` rehydrates by wrapping sampled arrays in `SampledTrack` lambdas.

### Chart formats
All three source formats converge to the same `ChartData` canonical representation:
- `official.hpp/.cpp` — Phigros official JSON
- `rpe.hpp/.cpp` — RPE JSON
- `pec.hpp/.cpp` — PEC format

Zip paths use the format `"path.zip:file.json"`. Difficulty sort order: EZ < HD < IN < AT < SP < EX.

### DebugFlag enum
`DebugFlag` in `include/phigros/app/debug_flags.hpp` is a `uint64_t` bitmask — currently bits 0–39 are used. New flags go on the next available bit, and `debug_flag_table()` must mirror the enum. `--debug-flags` accepts names, `ALL`, numeric masks, and `|`/`,`/`+` separators.

### RenderConfig / config files
- Config format: JSON with `//` line comments stripped before parsing — not full JSONC.
- Precedence (highest first): CLI flags → `--config` file → `RenderConfig` built-in defaults.
- Some fields exist in `RenderConfig` but are not loaded from JSON today (`force_line_alpha01`, `force_line_alpha01_by_lid`, `note_speed_mul_affects_travel`) — treat as code-level overrides.
- `no_cull_enter_time` is loaded from JSON but not written back by `config_to_json()`.

### Python binding scope boundary
The Python package intentionally does **not** expose: SDL windowing, texture ownership, backend executors, app/game-loop integration, respack loading, or video export. The public Python surface is `load_chart`, `load_config`, `simulate_autoplay`, `compile_chart`, `read_phbc`/`write_phbc`, `Chart`, `FrameEvaluator`, `AutoplayRun`, `CompiledChart`.

### Kinematics separation
Kinematics (`engine/kinematics.hpp`) solves base world-space geometry from line state, scroll, and note-local placement. Render-time control events then apply presentation modifiers (alpha, x-multiplier, y-offset, size, skew) **on top of** kinematics. Do not mix these layers.

### ChartScript
ChartScript (v2) is the JSON playlist DSL run by `phigros_render --script`. Config overrides inside ChartScript items use **flat** field names (not nested sections). Merge order: item-level config/mods > group config/mods > `defaults`. Use `scripts/gen_chartscript.py` to generate playlists from a chart directory.

### Doc maintenance rule
Update `cpp/docs/` when any of these change: public CLI flags, `RenderConfig` fields or defaults, core chart/runtime structures, chart or PHBC format behavior, CMake target names or major build flags, backend/platform support boundaries.

## Internal Doc Map

- `cpp/docs/ARCHITECTURE.md` — module layout and runtime flow
- `cpp/docs/DATA_STRUCTURES.md` — `Note`, `Line`, `ChartData`, `NoteState`, `PreparedChart`, `FrameSnapshot`
- `cpp/docs/INTERFACES.md` — public vs. internal boundary
- `cpp/docs/FORMAT.md` — chart formats and PHBC
- `cpp/docs/CHART_LOADER.md` — `scan_charts_directory`, folder/zip/JSON loading
- `cpp/docs/MATH.md` — easing, piecewise tracks, BPM mapping
- `cpp/docs/KINEMATICS.md` — coordinate model and note world-position evaluation
- `cpp/docs/RENDER.md` — frame construction pipeline
- `cpp/docs/CONFIG.md` — `RenderConfig` fields, defaults, clamps, serialization
- `cpp/docs/DEBUG_FLAGS.md` — all `--debug-flags` tokens
- `cpp/docs/BUILD_AND_TEST.md` — build switches, target families, verification workflow
- `docs/PYTHON_BINDINGS.md` — Python API reference
- `docs/CONFIG_USAGE.md` — config workflow for renderer, Python, and ChartScript
- `docs/CHARTSCRIPT.md` — full ChartScript DSL reference
