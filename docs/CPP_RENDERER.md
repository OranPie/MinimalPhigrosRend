# C++ Renderer Reference

> 🌐 [中文](CPP_RENDERER.zh.md)

`phigros_sdl_app` is the user-facing native renderer/player in this repository.

Use this page for build and runtime workflow. Use [../cpp/docs/](../cpp/docs/) for internal subsystem details. The older argv-based `phigros_render` executable remains available only when `BUILD_LEGACY_CLI=ON`.

## Quick Build

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --target phigros_sdl_app --parallel
```

Binary output:

- `cpp/build/phigros_sdl_app`

## Quick Run

```bash
./cpp/build/phigros_sdl_app
```

The app opens directly into the SDL chart library. Put chart folders or chart zips under `charts/`, then use the in-app screens:

- chart library and rescan
- chart detail
- settings
- play view
- pause menu
- result screen

The desktop, Android, and iOS builds share this SDL app entry instead of requiring a chart path from argv.

## Legacy CLI

```bash
cmake -S cpp -B cpp/build_cli -DBUILD_LEGACY_CLI=ON -DUSE_BGFX=OFF
cmake --build cpp/build_cli --target phigros_render --parallel
./cpp/build_cli/phigros_render charts/MyChart/IN.json --score-only
./cpp/build_cli/phigros_render charts/MyChart/IN.json --play
./cpp/build_cli/phigros_render charts/MyChart/IN.json --mode scriptplay --scriptplay docs/scriptplay_template.json
./cpp/build_cli/phigros_render charts/MyChart/IN.json --record out.mp4
./cpp/build_cli/phigros_render charts/MyChart/IN.json --benchmark --benchmark-iterations 20
./cpp/build_cli/phigros_render charts/MyChart/IN.json --config config/config.jsonc
```

## Supported Inputs

- `.json`: Official or RPE chart
- `.pec`: PEC chart
- `.phbc`: precompiled binary chart
- directory and zip-discovery workflows through `--list-charts` and chart loader helpers

For format internals, see [../cpp/docs/FORMAT.md](../cpp/docs/FORMAT.md) and [../cpp/docs/CHART_LOADER.md](../cpp/docs/CHART_LOADER.md).

## Common Legacy CLI Areas

Playback:

- `--mode <autoplay|manual|scriptplay>`
- `--play`
- `--scriptplay <file.json>`
- `--score-only`
- `--duration <sec>`
- `--truncate-at-duration`
- `--audio-offset <ms>`
- `--playback-speed <mul>`
- `--width <px>` / `--height <px>`
- `--headless`

Visual overrides:

- `--approach <sec>`
- `--chart-speed <mul>`
- `--expand <factor>`
- `--note-scale-x <mul>` / `--note-scale-y <mul>`
- `--note-alpha <0-1>`
- `--font-size <mul>`
- `--overlay-transparent`
- `--debug-flags <flags>`

Asset overrides:

- `--respack <path>`
- `--bg <path>`
- `--font <path>`
- `--audio <path>`

Replay and recording:

- `--save-replay <file>` / `--play-replay <file>`
- `--record <out.mp4>`
- `--record-preset`, `--record-codec`, `--record-hw`
- `--record-fps`, `--sim-fps`
- `--record-resolution`, `--record-capture-resolution`
- `--record-queue-depth <n>`
- `--record-start`, `--record-end`
- `--screenshot-dir <dir>`, `--screenshot-fps <fps>`

Compile and script workflow:

- `--compile <out.phbc>`
- `--sample-rate <Hz>`
- `--compress [zlib|lzma]`
- `--encrypt [aes-gcm|aes-cbc|chacha20|xor]`
- `--password <passphrase>`
- `--script <file.chartscript.json>`
- `--scriptplay <file.json>`
- `--mod <file.mod.json>`

Utility and logging:

- `--info`, `--version`, `--help`
- `--list-charts <dir>`
- `--backend <name>`
- `--profile`, `--record-profile`
- `--log-level`, `--log-filter`, `--log-file`
- `--log-no-color`, `--log-time`
- `--trace`, `--verbose`, `--quiet`

## Config

The SDL app currently uses built-in defaults and in-app toggles for its runtime settings. The legacy CLI can still load a JSON/JSONC-style config:

```bash
./cpp/build_cli/phigros_render charts/MyChart/IN.json --config config/config.jsonc
```

Read these next:

- Shared usage guidance: [CONFIG_USAGE.md](CONFIG_USAGE.md)
- Internal field reference: [../cpp/docs/CONFIG.md](../cpp/docs/CONFIG.md)
- Debug overlays: [../cpp/docs/DEBUG_FLAGS.md](../cpp/docs/DEBUG_FLAGS.md)

## Related Docs

- C++ quickstart: [../cpp/README.md](../cpp/README.md)
- Python bindings: [PYTHON_BINDINGS.md](PYTHON_BINDINGS.md)
- ChartScript: [CHARTSCRIPT.md](CHARTSCRIPT.md)
- ScriptPlay DSL: [SCRIPTPLAY.md](SCRIPTPLAY.md)
- Internal render pipeline: [../cpp/docs/RENDER.md](../cpp/docs/RENDER.md)
- Internal architecture: [../cpp/docs/ARCHITECTURE.md](../cpp/docs/ARCHITECTURE.md)
