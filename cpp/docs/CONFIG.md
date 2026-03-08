# Configuration Reference

> 🌐 [中文](CONFIG.zh.md)
All options are loaded from a JSONC file (JSON with `//` and `#` line comments).

```bash
./phigros_render chart.json --config config.jsonc
```

---

## Top-level structure

```jsonc
{
  "backend":  "sdl",
  "window":   { ... },
  "render":   { ... },
  "assets":   { ... },
  "gameplay": { ... },
  "rpe":      { ... },
  "debug":    { ... }
}
```

---

## `backend` (top-level)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `backend` | string | `"sdl3_bgfx"` | Runtime renderer mode. Supported runtime values are `sdl`/`sdl_hw` (prefer hardware) and `sdl_sw` (prefer software). |

You may also set `render.backend`; top-level `backend` takes precedence.

---

## `window`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `w` | int | `1280` | Window / output width in pixels |
| `h` | int | `720`  | Window / output height in pixels |

---

## `render`

### Core visual

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `approach` | float | `3.0` | Note approach window (seconds). Clamped `[0.1, 30]`. |
| `chart_speed` | float | `1.0` | Playback speed multiplier. Clamped `[0.1, 20]`. |
| `expand` | float | `1.0` | Camera zoom/playfield expansion factor. |
| `overrender` | float | `1.0` | Overdraw factor for offscreen culling margin. |
| `no_cull` | bool | `false` | Disable all note culling (render every note every frame). |
| `no_cull_screen` | bool | `false` | Disable screen-boundary culling only (keep t_enter culling). |
| `no_cull_enter_time` | bool | `true` | When `false`, skip notes whose `t_enter` has not yet been reached (optimises dense charts). Default `true` keeps all notes that have entered the judge window regardless of screen position. |
| `note_scale_x` | float | `2.5` | Horizontal note size multiplier. |
| `note_scale_y` | float | `1.0` | Vertical note size multiplier. |
| `note_flow_speed_multiplier` | float | `1.0` | Per-note scroll speed multiplier. |
| `note_speed_mul_affects_travel` | bool | `false` | RPE: per-note `speed_mul` affects approach distance. |
| `note_alpha` | float | `1.0` | Global note opacity multiplier. Clamped `[0, 1]`. |
| `note_outline` | bool | `false` | Draw a dark outline at 1.08× note size before the note. |

### Line alpha → note alpha

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `line_alpha_affects_notes` | string | `"negative_only"` | How judge-line alpha modulates visible note alpha. |

Values for `line_alpha_affects_notes`:

| Value | Behaviour |
|-------|-----------|
| `"off"` | Notes unaffected by line alpha |
| `"negative_only"` | Notes dim when line alpha < 0.5 (default Phigros behaviour) |
| `"always"` | Note alpha = `note.alpha * line.alpha` always |

### Hit effects

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `show_hitfx` | bool | `true` | Render hit-effect sprites/rings on note judgement. |
| `show_particles` | bool | `true` | Render particle bursts on note judgement. |
| `particle_count` | int | `8` | Particles per hit burst. Clamped `[0, 64]`. |
| `hitfx_intensity` | float | `1.0` | Alpha multiplier for all hit effects. Clamped `[0, 2]`. |
| `hitfx_effect_apply` | bool | `true` | Whether hit effects participate in trail/motion blur passes. If `false`, hit effects render after compositing (crisp/no smear). |

### Trail effect

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `trail_alpha` | float | *(disabled)* | Base alpha for trail slots. Enables trail when set. |
| `trail_frames` | int | `6` | Number of trail buffer slots. |
| `trail_decay` | float | `0.75` | Alpha decay factor per slot (slot N = `trail_alpha × decay^N`). |
| `trail_blur` | int | `0` | Downscale factor for per-slot blur (0 = no blur). |
| `trail_dim` | int | `0` | Darkening overlay strength per historical slot (0–255). |
| `trail_blur_ramp` | bool | `false` | Increase blur amount for older slots. |
| `trail_blend` | string | `"blend"` | Compositing mode: `"blend"` or `"add"`. |

### Motion blur

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `motion_blur_samples` | int | *(disabled)* | Sub-frame sample count. Enables motion blur when set. |
| `motion_blur_shutter` | float | `0.5` | Shutter angle fraction `[0, 1]`. |

---

## `assets`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `respack` | string | `"./respack.zip"` | Path to resource pack ZIP. |
| `bg` | string | *(none)* | Path to background image (PNG/JPG). |
| `bg_blur` | int | `10` | Background blur downscale factor (0 = no blur). |
| `bg_dim` | int | `120` | Background dimming overlay opacity (0–255). |

---

## `gameplay`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `autoplay` | bool | `true` | Automatically judge all notes perfectly. |
| `hold_tail_tol` | float | `0.8` | Hold tail release tolerance (fraction of hold duration). |
| `hold_fx_interval_ms` | int | `200` | Minimum interval between hold-tick hit effects (ms). |
| `audio_offset_ms` | float | `0.0` | Audio latency compensation. Positive = advance notes. |

---

## `rpe`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `rpe_easing_shift` | int | `0` | RPE easing index offset for compatibility with non-standard exports. |

---

## `debug`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `basic_debug` | bool | `false` | Overlay FPS and visible note count on screen. |

---

## Force line alpha overrides

These are set via the C++ API or programmatically:

- `force_line_alpha01` — force all lines to a single alpha value `[0, 1]`
- `force_line_alpha01_by_lid` — map of `{line_id: alpha}` for per-line override

---

## Full example

```jsonc
// MinimalPhigrosRend C++ config
{
  "window": {
    "w": 1920,
    "h": 1080
  },

  "render": {
    "approach": 3.0,
    "chart_speed": 1.0,
    "expand": 1.0,
    "note_scale_x": 2.5,
    "note_scale_y": 1.0,
    "note_alpha": 1.0,
    "note_outline": false,
    "line_alpha_affects_notes": "negative_only",

    "show_hitfx": true,
    "show_particles": true,
    "particle_count": 8,
    "hitfx_intensity": 1.0,
    "hitfx_effect_apply": true,

    // Trail: uncomment to enable
    // "trail_alpha": 0.4,
    // "trail_frames": 6,
    // "trail_decay": 0.7,

    // Motion blur: uncomment to enable
    // "motion_blur_samples": 4,
    // "motion_blur_shutter": 0.5
  },

  "assets": {
    "respack": "./respack.zip",
    "bg_blur": 10,
    "bg_dim": 120
  },

  "gameplay": {
    "autoplay": true,
    "hold_tail_tol": 0.8,
    "hold_fx_interval_ms": 200,
    "audio_offset_ms": 0.0
  },

  "debug": {
    "basic_debug": false
  }
}
```

---

## Round-trip serialization

The C++ API can save the current config back to JSON:

```cpp
#include "phigros/config/render_config.hpp"
phigros::config::save_config("out.json", cfg);
```
