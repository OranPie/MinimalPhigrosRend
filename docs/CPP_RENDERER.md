# C++ Renderer — Reference Guide

> 🌐 [中文](CPP_RENDERER.zh.md)
The C++ renderer (`phigros_render`) is the high-performance, headless-capable native renderer
for Phigros charts. It shares no runtime dependency with the Python `phic_renderer` and
targets desktop (SDL 2/3) and WebAssembly (Emscripten) platforms.

---

## Table of Contents

1. [Building](#building)
2. [CLI Reference](#cli-reference)
3. [Config File Reference](#config-file-reference)
4. [Visual Effects](#visual-effects)
5. [Chart Script Mode](#chart-script-mode)
6. [Keybindings](#keybindings)
7. [Config Recipes](#config-recipes)

---

## Building

### Desktop (SDL)

```bash
cd cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) phigros_render
```

The binary is at `cpp/build/phigros_render`.

### WebAssembly (Emscripten)

```bash
cd cpp
mkdir -p build_wasm && cd build_wasm
emcmake cmake .. -DUSE_SDL3=OFF -DUSE_BGFX=OFF
emmake make -j$(nproc)
# Serve with the included preview server:
python3 scripts/serve.py
```

### Run Tests

```bash
cd cpp/build
./test_easing
./test_engine
```

### Benchmark

```bash
./phigros_render chart.json --benchmark --benchmark-iterations 20
```

---

## CLI Reference

```
Usage: phigros_render <chart_path> [options]
```

The `chart_path` can be the first positional argument, or supplied via any supported path form.

### Chart Formats

| Extension | Format |
|-----------|--------|
| `.json`   | Official or RPE chart |
| `.pec`    | PEC legacy chart |
| `.phbc`   | Pre-compiled binary (fastest load) |

### Playback

| Flag | Description |
|------|-------------|
| `--play` | Interactive mode (mouse/touch input) |
| `--score-only` | Headless engine scoring (fastest, no window) |
| `--duration <sec>` | Auto-quit after N seconds |
| `--audio-offset <ms>` | Audio latency compensation (positive = advance notes) |
| `--width <px>` | Window width (overrides config) |
| `--height <px>` | Window height (overrides config) |

### Assets

| Flag | Description |
|------|-------------|
| `--config <path>` | Render config JSON/JSONC file |
| `--respack <path>` | Respack ZIP (skins, hit sounds) |
| `--bg <path>` | Background image |
| `--font <path>` | TTF font file |
| `--audio <path>` | BGM audio file (overrides chart-embedded audio) |

### Mods

| Flag | Description |
|------|-------------|
| `--mod <file.mod.json>` | Apply a mod to the chart (repeatable, applied in order) |

See `docs/ADVANCE_MODE_GUIDE.md` for the mod format reference.

### Chart Script

| Flag | Description |
|------|-------------|
| `--script <file.chartscript.json>` | Run a declarative chart playlist |

See `docs/CHARTSCRIPT.md` for the full chartscript DSL reference.

### Compile

| Flag | Description |
|------|-------------|
| `--compile <out.phbc>` | Compile chart to binary and exit |
| `--sample-rate <Hz>` | Sampling rate for compile (default: 240) |

Pre-compiling reduces load time on repeated runs and is recommended for playlists.

### Recording

| Flag | Description |
|------|-------------|
| `--record <output.mp4>` | Record video (headless) |
| `--record-preset <name>` | `fast` \| `balanced` \| `quality` \| `archive` |
| `--record-codec <codec>` | `libx264` \| `libx265` \| `libvpx-vp9` |
| `--record-fps <fps>` | Recording framerate (default: 60) |
| `--record-resolution WxH` | e.g. `1920x1080` |
| `--record-start <sec>` | Start recording at time |
| `--record-end <sec>` | Stop recording at time |

### Replay

| Flag | Description |
|------|-------------|
| `--save-replay <file.rep>` | Save replay from a `--play` session |
| `--play-replay <file.rep>` | Play back a saved replay |

### Analysis / Utility

| Flag | Description |
|------|-------------|
| `--info` / `-i` | Print chart metadata (line/note counts, offset) and exit |
| `--list-charts <dir>` | Discover and list all charts under a directory |
| `--benchmark` | Run engine benchmark (implies `--score-only`) |
| `--benchmark-iterations N` | Number of benchmark runs (default: 10) |

### Other

| Flag | Description |
|------|-------------|
| `--headless` | No visible window |
| `--screenshot-dir <dir>` | Save PNG screenshots every 5 s |
| `--backend <name>` | Renderer backend (`sdl`, `sdl_hw`, `sdl_sw`) |
| `--version` / `-v` | Print version and exit |
| `--help` / `-h` | Print help and exit |

---

## Config File Reference

The renderer accepts a JSON (or JSONC — JSON with `//` comments) config file via `--config`.

### Full Example

```jsonc
// phigros_render config (JSONC — // comments are supported)
{
  "window": {
    "w": 1280,
    "h": 720
  },

  "render": {
    // Chart timing
    "approach": 3.0,               // approach time in seconds (0.1 – 30)
    "chart_speed": 1.0,            // chart speed multiplier (0.1 – 20)

    // Note visuals
    "expand": 1.0,                 // horizontal lane width factor
    "note_scale_x": 2.5,           // note width scale
    "note_scale_y": 1.0,           // note height scale
    "note_flow_speed_multiplier": 1.0,
    "note_alpha": 1.0,             // global note alpha [0, 1]
    "note_outline": false,         // draw note outline border

    // Judge-line alpha → note alpha coupling
    // "off" | "negative_only" (default) | "always"
    "line_alpha_affects_notes": "negative_only",

    // Culling (skip off-screen objects — keep true for performance)
    "no_cull": false,
    "no_cull_screen": false,
    "no_cull_enter_time": true,
    "overrender": 1.0,             // extend cull rect by this factor

    // Hit effects
    "show_hitfx": true,
    "show_particles": true,
    "particle_count": 8,           // particles per hit burst (0 – 64)
    "hitfx_intensity": 1.0,        // alpha multiplier for all hit effects [0, 2]

    // ── Trail effect ────────────────────────────────────────────────────────
    "trail_alpha": 0.5,            // ghost opacity [0, 1]
    "trail_frames": 8,             // ring buffer depth (ghost count)
    "trail_decay": 0.85,           // per-frame alpha multiplier
    "trail_blur": 2,               // blur radius steps
    "trail_dim": 20,               // darkening per frame [0, 255]
    "trail_blur_ramp": true,       // apply blur ramp (oldest ghost = most blur)
    "trail_blend": "alpha",        // "alpha" | "add"

    // Enhanced trail (new in v2)
    "trail_blur_quality": 2,       // downscale passes for blur chain (1 – 4)
    "trail_chromatic": 1.2,        // chromatic aberration offset px per age (0 = off)
    "trail_decay_curve": "gaussian", // "exponential" (default) | "gaussian"
    "trail_glow": 0.3,             // additive glow intensity per ghost (0 = off)

    // ── Motion blur ─────────────────────────────────────────────────────────
    "motion_blur_samples": 4,      // accumulation samples (2 – 16)
    "motion_blur_shutter": 0.5,    // shutter angle fraction (0.1 – 1.0)
    "motion_blur_curve": "gaussian" // "uniform" (default) | "gaussian"
  },

  "assets": {
    "respack": "./respack.zip",
    "bg": null,                    // background image path
    "bg_blur": 10,                 // background blur strength (downscale factor)
    "bg_dim": 120                  // background dim overlay [0, 255]
  },

  "gameplay": {
    "autoplay": true,              // auto-play (no input required)
    "hold_tail_tol": 0.8,          // hold early-release tolerance [0, 1]
    "hold_fx_interval_ms": 200,    // hold tick hit-fx interval (ms)
    "audio_offset_ms": 0.0         // positive = advance notes relative to audio
  },

  "rpe": {
    "rpe_easing_shift": 0          // easing index shift for RPE charts
  },

  "debug": {
    "basic_debug": false           // show FPS and note count overlay
  }
}
```

### Field Quick Reference

#### `window`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `w` | int | 1280 | Window width in pixels |
| `h` | int | 720 | Window height in pixels |

#### `render`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `approach` | float | 3.0 | Approach time (seconds) |
| `chart_speed` | float | 1.0 | Chart speed multiplier |
| `expand` | float | 1.0 | Lane width factor |
| `note_scale_x` | float | 2.5 | Note width scale |
| `note_scale_y` | float | 1.0 | Note height scale |
| `note_flow_speed_multiplier` | float | 1.0 | Note flow speed |
| `note_alpha` | float | 1.0 | Global note opacity |
| `note_outline` | bool | false | Draw note outlines |
| `line_alpha_affects_notes` | string | `"negative_only"` | `"off"` / `"negative_only"` / `"always"` |
| `no_cull` | bool | false | Disable all culling |
| `overrender` | float | 1.0 | Cull rect extension factor |
| `show_hitfx` | bool | true | Show hit effects |
| `show_particles` | bool | true | Show particles |
| `particle_count` | int | 8 | Particles per hit |
| `hitfx_intensity` | float | 1.0 | Hit effect brightness |

#### Trail Effect

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `trail_alpha` | float | — | Ghost opacity [0, 1] |
| `trail_frames` | int | — | Ghost ring buffer depth |
| `trail_decay` | float | — | Per-frame alpha multiplier |
| `trail_blur` | int | — | Blur radius steps |
| `trail_dim` | int | — | Darkening per frame [0, 255] |
| `trail_blur_ramp` | bool | — | Oldest ghost = most blur |
| `trail_blend` | string | — | `"alpha"` or `"add"` |
| `trail_blur_quality` | int | 2 | Downscale passes (1 – 4); higher = softer blur |
| `trail_chromatic` | float | 0 | Chromatic offset px per age (0 = disabled) |
| `trail_decay_curve` | string | `"exponential"` | `"exponential"` or `"gaussian"` |
| `trail_glow` | float | 0 | Additive glow intensity per ghost (0 = disabled) |

#### Motion Blur

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `motion_blur_samples` | int | — | Accumulation samples (2 – 16) |
| `motion_blur_shutter` | float | — | Shutter angle fraction (0.1 – 1.0) |
| `motion_blur_curve` | string | `"uniform"` | `"uniform"` or `"gaussian"` (center-weighted) |

#### `assets`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `respack` | string | `"./respack.zip"` | Respack ZIP path |
| `bg` | string | — | Background image |
| `bg_blur` | int | 10 | Background blur (downscale factor) |
| `bg_dim` | int | 120 | Background dim overlay [0, 255] |

#### `gameplay`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `autoplay` | bool | true | Auto-play mode |
| `hold_tail_tol` | float | 0.8 | Hold early-release tolerance |
| `hold_fx_interval_ms` | int | 200 | Hold tick fx interval (ms) |
| `audio_offset_ms` | float | 0.0 | Audio offset compensation |

---

## Visual Effects

### Trail Effect

The trail renderer maintains a ring buffer of past frames. On each rendered frame, older
ghosts are composited behind the current frame with decreasing opacity.

**`trail_decay_curve`**
- `"exponential"` (default): each frame multiplies alpha by `trail_decay`. Fast falloff.
- `"gaussian"`: soft bell-curve falloff centered on the latest ghost. Produces a smoother,
  more cinematic look.

**`trail_blur_quality`**  
Applies a downscale → upscale blur chain to each ghost. Each pass halves resolution and
back-upscales. A value of 2–3 gives a soft diffusion without being too expensive.
Has no effect on WebAssembly builds.

**`trail_chromatic`**  
Each ghost is offset by `trail_chromatic × age` pixels in the R and B channels, creating
a colour-fringe ghosting effect. Values of 0.5–2.0 are typically noticeable.

**`trail_glow`**  
Each ghost is also composited once more in additive blend mode with intensity `trail_glow`,
creating a bloom-like glow around moving objects.

### Motion Blur

The motion blur renderer accumulates multiple sub-frame samples weighted by a shutter curve.

**`motion_blur_samples`**: Higher values are smoother but more expensive. 4 is a good default.

**`motion_blur_shutter`**: Controls the time window of each frame that contributes to the
blur. 0.5 = 180° shutter angle (cinematic). 1.0 = full-frame exposure.

**`motion_blur_curve`**
- `"uniform"`: equal weight for all samples.
- `"gaussian"`: centre-weighted, giving more influence to the central frame. Produces
  a softer, less harsh blur.

---

## Chart Script Mode

See **[CHARTSCRIPT.md](CHARTSCRIPT.md)** for the full declarative playlist DSL reference.

Quick start:

```bash
phigros_render --script my_playlist.chartscript.json
```

Or generate a playlist from a chart directory with:

```bash
python3 scripts/gen_chartscript.py --charts_dir charts/ --output my_playlist.chartscript.json
```

---

## Keybindings

These are active in interactive (`--play`) and default autoplay modes:

| Key | Action |
|-----|--------|
| `Space` | Pause / resume |
| `R` | Restart chart from beginning |
| `Esc` | Quit |

---

## Config Recipes

### Showcase (high quality, cinematic)

```jsonc
{
  "render": {
    "trail_alpha": 0.7,
    "trail_frames": 12,
    "trail_decay": 0.88,
    "trail_decay_curve": "gaussian",
    "trail_blur_quality": 3,
    "trail_chromatic": 1.5,
    "trail_glow": 0.35,
    "motion_blur_samples": 6,
    "motion_blur_shutter": 0.6,
    "motion_blur_curve": "gaussian",
    "show_particles": true,
    "particle_count": 12
  }
}
```

### Ambient (soft, low-distraction)

```jsonc
{
  "render": {
    "trail_alpha": 0.4,
    "trail_frames": 8,
    "trail_decay": 0.80,
    "trail_decay_curve": "gaussian",
    "trail_blur_quality": 2,
    "trail_glow": 0.15,
    "motion_blur_samples": 4,
    "motion_blur_shutter": 0.5
  }
}
```

### Battle (intense, fast)

```jsonc
{
  "render": {
    "trail_alpha": 0.85,
    "trail_frames": 6,
    "trail_decay": 0.92,
    "trail_blend": "add",
    "trail_chromatic": 2.0,
    "trail_glow": 0.5,
    "motion_blur_samples": 8,
    "motion_blur_shutter": 0.8,
    "motion_blur_curve": "uniform",
    "hitfx_intensity": 1.5,
    "particle_count": 16
  }
}
```

### Minimal (clean, no effects)

```jsonc
{
  "render": {
    "show_particles": false,
    "show_hitfx": false,
    "note_outline": false
  }
}
```
