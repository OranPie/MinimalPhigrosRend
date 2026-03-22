# Configuration Reference

> 🌐 [中文](CONFIG.zh.md)

This page documents the current `RenderConfig` implementation in `include/phigros/config/render_config.hpp`.

For user workflow and precedence examples, see [../../docs/CONFIG_USAGE.md](../../docs/CONFIG_USAGE.md).

## Input Format

The loader path is:

- `load_config(path)`
- `load_config_text(text)`
- `load_config_json(json)`

Current behavior is best described as JSON plus stripped `//` line comments before parsing. Treat the code as the source of truth rather than assuming full JSONC support.

## Top-Level Shape

```json
{
  "backend": "...",
  "window": { "w": 1280, "h": 720 },
  "render": { ... },
  "assets": { ... },
  "gameplay": { ... },
  "rpe": { ... },
  "debug": { ... }
}
```

`render.backend` is also read as an alias, but top-level `backend` wins if both are present.

## JSON-Loaded Fields

### `window`

- `w`: default `1280`
- `h`: default `720`

### `render`

Core visual and layout:

- `approach`: default `3.0`, clamped to `[0.1, 30.0]`
- `chart_speed`: default `1.0`, clamped to `[0.1, 20.0]`
- `expand`: maps to `expand_factor`, default `1.0`
- `note_scale_x`: default `2.5`
- `note_scale_y`: default `1.0`
- `note_flow_speed_multiplier`: default `1.0`
- `note_alpha`: default `1.0`, clamped to `[0.0, 1.0]`
- `font_size`: default `1.0`, clamped to `[0.5, 3.0]`
- `font_align`: default `true`
- `overlay_transparent`: default `false`
- `overrender`: default `1.0`
- `note_outline`: default `false`

Visibility and path behavior:

- `no_cull`: default `false`
- `no_cull_screen`: default `false`
- `no_cull_enter_time`: default `true`

Line alpha mode:

- `line_alpha_affects_notes`: `off`, `negative_only`, or `always`
- current `negative_only` behavior follows the runtime code path and only applies line-alpha modulation when the raw line alpha is negative

Hold and hit effects:

- `hold_body_glow_alpha`: default `0.35`, clamped to `[0.0, 1.0]`
- `show_hitfx`: default `true`
- `show_particles`: default `true`
- `particle_count`: default `8`, clamped to `[0, 64]`
- `hitfx_intensity`: default `1.0`, clamped to `[0.0, 2.0]`
- `hitfx_effect_apply`: default `true`

Trail options, all optional:

- `trail_alpha`
- `trail_frames`
- `trail_decay`
- `trail_blur`
- `trail_dim`
- `trail_blur_ramp`
- `trail_blend`
- `trail_blur_quality`
- `trail_chromatic`
- `trail_decay_curve`
- `trail_glow`

Motion blur options, all optional:

- `motion_blur_samples`
- `motion_blur_shutter`
- `motion_blur_curve`

Backend alias:

- `backend`: stored into `cfg.backend` if present in `render`

### `assets`

- `respack`: maps to `respack_path`, default `./respack.zip`
- `bg`: default empty string
- `bg_blur`: default `10`
- `bg_dim`: default `120`

### `gameplay`

- `autoplay`: default `true`
- `mode`: `autoplay` | `manual` | `scriptplay`, default `autoplay`
- `judge_script`: default empty string, used when `mode == "scriptplay"`
- `hold_tail_tol`: default `0.8`
- `hold_fx_interval_ms`: default `200`
- `audio_offset_ms`: default `0.0`

Nested `simulateplay` object:

- `enabled`: default `false`
- `mode`: default `aggressive`
- `max_pointers`: default `2`, clamped to `[1, 8]`
- `jitter_ms`: default `12.0`, clamped to `[0.0, 80.0]`
- `render_pointer`: default `true`
- `render_trail`: default `true`
- `trail_seconds`: default `0.16`, clamped to `[0.02, 1.0]`
- `cursor_radius_px`: default `20.0`, clamped to `[4.0, 80.0]`

### `rpe`

- `rpe_easing_shift`: default `0`

### `debug`

- `basic_debug`: default `false`

### top-level `backend`

- `backend`: default `sdl3_bgfx`
- if present at the top level, it overrides `render.backend`

## Programmatic-Only Or Not Fully Wired Fields

These exist in `RenderConfig` but are not loaded from JSON by `load_config_json()` today:

- `force_line_alpha01`
- `force_line_alpha01_by_lid`
- `note_speed_mul_affects_travel`

Treat them as code-level overrides unless the loader is extended.

## Serialization

`config_to_json()` and `save_config()` serialize the config back to JSON.

Current serialization behavior:

- writes the canonical section structure shown above
- always writes the nested `gameplay.simulateplay` block
- writes top-level `backend`
- omits `assets.bg` when empty
- omits optional trail/motion-blur fields when their optionals are not set
- some optional values are emitted with simple truthy checks, so false-like values may be omitted in the current implementation

## Related Docs

- [../../docs/CONFIG_USAGE.md](../../docs/CONFIG_USAGE.md)
- [RENDER.md](RENDER.md)
- [DEBUG_FLAGS.md](DEBUG_FLAGS.md)
