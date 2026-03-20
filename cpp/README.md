# MinimalPhigrosRend — C++ Renderer

> 🌐 [中文](README.zh.md)

High-performance Phigros chart renderer rewritten in C++17.  
Cross-platform: Desktop (SDL3), Web (WASM/Emscripten + SDL2), Mobile.

Python chart-processing bindings are also available via `phigros_cpp`.
See [`../docs/PYTHON_BINDINGS.md`](../docs/PYTHON_BINDINGS.md).

## Features

- Parses **Official**, **RPE**, and **PEC** chart formats
- Autoplay with hit-effect rendering (hitfx + particles)
- Interactive play mode (`--play`) with keyboard/touch input
- Replay record + playback (`--save-replay` / `--play-replay`)
- Video export via FFmpeg subprocess (`--record`)
- Trail effect, motion blur, note outline, hold-glow
- SDL render backends (`--backend sdl|sdl_hw|sdl_sw`)
- WASM build for browser deployment

## Quick Start

### Build (Desktop SDL3)

```bash
cd cpp
mkdir build && cd build
cmake .. -DUSE_BGFX=OFF -DUSE_SDL3=ON
make -j$(nproc)
```

### Run

```bash
# Autoplay (headless score only)
./phigros_render charts/MyChart/IN.json --score-only

# Autoplay with window
./phigros_render charts/MyChart/IN.json

# Interactive play mode
./phigros_render charts/MyChart/IN.json --play

# Benchmark (20 iterations, headless)
./phigros_render charts/MyChart/IN.json --benchmark --benchmark-iterations 20

# Record video (requires FFmpeg in PATH)
./phigros_render charts/MyChart/IN.json --record output.mp4 --record-preset balanced --record-fps 60

# 20s capture, duration-truncated scoring denominator
./phigros_render charts/MyChart/IN.json --record out_20s.mp4 --duration 20 --truncate-at-duration
```

### Key Bindings (play mode)

| Key | Action |
|-----|--------|
| D / F / J / K | Hit notes |
| Space | Pause / Resume |
| R | Restart |
| Esc | Quit |

## Build Options

| CMake Flag | Default | Description |
|-----------|---------|-------------|
| `USE_SDL3` | `ON` | Use SDL3 (OFF = SDL2 for WASM) |
| `USE_BGFX` | `OFF` | Enable bgfx GPU backend |

### WASM Build

```bash
emcmake cmake .. -DUSE_SDL3=OFF -DUSE_BGFX=OFF
emmake make -j$(nproc)
# Output: phigros.html + phigros.js + phigros.wasm
```

## Configuration

Renderer behaviour is controlled via a JSONC config file:

```bash
./phigros_render charts/MyChart/IN.json --config my_config.jsonc
```

See [`docs/CONFIG.md`](docs/CONFIG.md) for a full reference of all config fields.

**Example config** (save as `config.jsonc`):

```jsonc
{
  "window": { "w": 1280, "h": 720 },
  "render": {
    "approach": 3.0,
    "note_scale_x": 2.5,
    "note_outline": true,
    "line_alpha_affects_notes": "negative_only"
  },
  "assets": {
    "respack": "./respack.zip",
    "bg_dim": 120
  },
  "gameplay": {
    "autoplay": true
  }
}
```

## CLI Reference

```
./phigros_render <chart_path> [options]

Input:
  --config <path>              JSONC config file

Playback:
  --play                       Interactive play mode
  --score-only                 Print score and exit (headless)
  --duration <sec>             Auto-quit after N seconds
  --truncate-at-duration       Use notes within duration as score denominator
  --audio-offset <ms>          Audio latency compensation (ms)
  --width <px>                 Window width override
  --height <px>                Window height override
  --headless                   Run without window (for benchmarking/CI)

Replay:
  --save-replay <path>         Save replay to file after play session
  --play-replay <path>         Load and replay a saved replay

Video:
  --record <output.mp4>        Record to video file (requires FFmpeg)
  --record-preset <name>       fast|balanced|quality|archive
  --record-codec <codec>       libx264/libx265/libvpx-vp9/h264_nvenc/...
  --record-hw <type>           nvenc|qsv|vaapi|amf|videotoolbox
  --record-fps <fps>           Recording framerate (default: 60)
  --sim-fps <fps>              Internal simulation sampling rate (default: 240)
  --record-resolution WxH      Output video resolution
  --record-capture-resolution WxH  Render/readback resolution
  --record-queue-depth <N>     Async encoder queue depth
  --record-start <sec>         Start time for recording
  --record-end <sec>           End time for recording

Benchmark:
  --benchmark                  Run in benchmark mode (headless)
  --benchmark-iterations <N>   Number of benchmark passes (default: 10)
  --profile                    Print per-phase frame timing stats

Screenshot:
  --screenshot-dir <path>      Directory to save PNG frames
  --screenshot-fps <fps>       Screenshot rate in chart-seconds

Backend:
  --backend sdl|sdl_hw|sdl_sw  Rendering backend

Audio:
  --audio <path>               Path to BGM audio file
Misc:
  --info/-i                    Print chart metadata and exit
  --list-charts <dir>          Discover charts under directory
  --version/-v                 Print version and exit
```

## Score And Duration

- Score formula: `score = int(real_acc * 900000 + max_combo/total_notes * 100000)`.
- `total_notes` is playable notes (`fake=false`), not raw `notes.size()`.
- With `--duration N`, recording progress uses `N` as end target (reaches `100.0%` at stop).
- With `--duration N --truncate-at-duration`, denominator is truncated to notes inside the duration window:
  - non-hold: `t_hit <= N`
  - hold: `t_end <= N`

## Testing

```bash
# Engine tests (6343 checks)
cd cpp/build
./test_engine --auto-discover ../../charts

# Parser tests (54 checks)
cd /path/to/MinimalPhigrosRend
./cpp/build/test_parser charts
```

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for a detailed overview of the module structure, data flow, and rendering pipeline.

## Performance

Typical benchmark results (headless, 240 Hz simulation):

| Chart | Notes | Speed |
|-------|-------|-------|
| AbsoluTedisoRdeR (RPE) | 1600 | ~450× realtime |
| Rrharil (Official) | 1300 | ~467× realtime |
| ATHAZA (RPE) | 1137 | ~514× realtime |
| Aleph0 (RPE) | 885 | ~606× realtime |
| Radiance (Official) | 667 | ~658× realtime |
| BetterGraphicAnimation | 616 | ~712× realtime |

Key optimisations: binary-search note visibility window, precomputed `cos_rot`/`sin_rot` per line, binary-search track seek, stack-allocated line-state array.
