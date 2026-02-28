# C++ Core Port (Phase 1 Scaffold)

This directory contains a C++ core-first implementation scaffold for the Phic renderer migration plan.

## Included

- `core/`: portable engine, nlohmann-json parser, mod pipeline, judgment loop, frame-command generation
- `core_c_api/`: stable C ABI facade for native and web wrappers (includes auto-simulate step)
- `desktop_app/`: CLI runner with merged input/advance/playlist execution flow
- `../phic_web/`: TypeScript + Vite web shell around WASM runtime
- `tests/`: baseline engine/parser/mod tests

## Native Build

```bash
cmake -S phic_port -B phic_port/build
cmake --build phic_port/build -j
ctest --test-dir phic_port/build --output-on-failure
```

## Web Build (Emscripten)

```bash
emcmake cmake -S phic_port -B phic_port/build-web -DPHIC_BUILD_WEB=ON -DPHIC_BUILD_DESKTOP_APP=OFF -DPHIC_BUILD_TESTS=OFF
cmake --build phic_port/build-web -j
```

Web artifacts are emitted to `phic_web/wasm/`.

## Current status

- Judgment windows/weights aligned with Python constants (`0.045/0.090/0.150`, weights `1.0/0.6/0/0`)
- Note kind IDs aligned with Python (`tap=1, drag=2, hold=3, flick=4`) with format-aware parsing (Official vs RPE type ids)
- Official parser applies BPM unit-time conversion; RPE parser applies beat/BPM map conversion with `bpmfactor`
- JSON chart parsing uses `nlohmann::json`
- JSONC config preprocessing supports comments + trailing commas
- Core mods support mirror/reverse/randomize/thin_out/transpose/stretch/quantize/wave/stutter/hold_convert
- Core mods now also support `full_blue`, lane-scale (`scale.x`), `compress_zip`, `attach` (lane/time subset), `fade` (time/constant), and note rules/overrides subset (`kind`/`speed_mul`/`alpha`/`side`)
- Simulateplay auto-input is available in C API via `phic_engine_step_auto`
- Judge events are exposed in C API via `phic_engine_step_ex` / `phic_engine_step_auto_ex`
- ABI v5 adds `phic_engine_step_v2` / `phic_engine_step_auto_v2` with `note_kind` and hold timing metadata
- Test suite includes a Python-vs-C++ parity oracle (`phic_parity_oracle`)
- Desktop run planner merges single input + advance tracks + playlist discovery

## Next steps

- Expand parser compatibility against full Official/RPE/PEC fixture corpus
- Add WebGL renderer option after Canvas2D baseline stabilizes
- Add browser integration tests (Playwright)
- Implement Windows-compatible ffmpeg process piping
