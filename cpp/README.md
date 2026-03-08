# MinimalPhigrosRend — C++ Renderer

> 🌐 [中文](README.zh.md)

High-performance Phigros chart renderer rewritten in C++17.  
Cross-platform: Desktop (SDL3), Web (WASM/Emscripten + SDL2), Mobile.

## Features

- Parses **Official**, **RPE**, and **PEC** chart formats
- Autoplay with hit-effect rendering (hitfx + particles)
- Interactive play mode (`--play`) with keyboard/touch input
- Replay record + playback (`--save-replay` / `--play-replay`)
- Video export via FFmpeg subprocess (`--record`)
- Trail effect, motion blur, note outline, hold-glow
- bgfx GPU renderer backend (`--backend bgfx`)
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
./phigros_render charts/MyChart/IN.json --record output.mp4 --record-preset medium
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
./phigros_render <chart.json> [options]

Input:
  --config <path>              JSONC config file

Playback:
  --play                       Interactive play mode
  --score-only                 Print score and exit (headless)
  --headless                   Run without window (for benchmarking/CI)

Replay:
  --save-replay <path>         Save replay to file after play session
  --play-replay <path>         Load and replay a saved replay

Video:
  --record <output.mp4>        Record to video file (requires FFmpeg)
  --record-preset <name>       FFmpeg preset (ultrafast/medium/slow)
  --record-fps <int>           Recording framerate (default: 60)
  --record-start <sec>         Start time for recording
  --record-end <sec>           End time for recording

Benchmark:
  --benchmark                  Run in benchmark mode (headless)
  --benchmark-iterations <N>   Number of benchmark passes (default: 3)

Screenshot:
  --screenshot-dir <path>      Directory to save PNG frames

Backend:
  --backend sdl3|bgfx          Rendering backend (default: sdl3)

Audio:
  --audio <path>               Path to BGM audio file
  --audio-offset <ms>          Audio latency compensation (ms)
```

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
