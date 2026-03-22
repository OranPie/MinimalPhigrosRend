# Data Structures

> 🌐 [中文](DATA_STRUCTURES.zh.md)

This page covers the core data model shared across parsing, engine, rendering, and bindings.

## Core Chart Model

### `Note`

Key fields:

- identity: `nid`, `line_id`, `kind`
- placement: `above`, `x_local_px`, `y_offset_px`
- timing: `t_hit`, `t_end`, `visible_time`
- appearance: `size_px`, `alpha01`, `tint_rgb`, optional `tint_hitfx_rgb`
- runtime caches: `scroll_hit`, `scroll_end`, `t_enter`, `mh`
- format-specific extras: `hitsound_path`, `speed_mul`, `fake`

### `Line`

A line owns time-varying evaluators and RPE-specific presentation state:

- base tracks: `pos_x`, `pos_y`, `rot`, `alpha`, `scroll_px`
- compiled overrides: `scroll_fn`, `compiled_color`
- display state: `color_rgb`, `texture_path`, `text`, `anchor`, `name`
- hierarchy/state: `father`, `rotate_with_father`, `attach_ui`, `z_order`, `is_cover`
- RPE controls: `alpha_ctrl`, `pos_ctrl`, `size_ctrl`, `y_ctrl`, `skew_ctrl`

### `ChartData`

Canonical parsed chart container:

- `offset`
- `lines`
- `notes`
- optional RPE metadata asset paths
- cached `chart_end_t` and `playable_count`
- `is_compiled` flag for PHBC-derived charts
- indexes: `early_notes`, `notes_by_enter`

## Runtime State

### `NoteState`

Per-note mutable judge state:

- current judgment state: `judged`, `hit`, `miss`, `judge_grade`
- hold lifecycle: `holding`, `released_early`, `hold_finalized`, `hold_failed`, `hold_grade`
- timing caches: `judge_t`, `judge_delta_ms`, `release_t`, `next_hold_fx_ms`

### `PreparedChart`

Binding/API-side packaged input:

- `ChartData chart`
- `RenderConfig config`
- `scoring_notes`
- `simulation_end`

## Discovery and Format Containers

### `ChartAssets` and `ChartEntry`

Used by chart discovery and asset resolution:

- `ChartAssets`: music path, illustration path, extra files
- `ChartEntry`: display name, difficulty, chart path, assets, source type

### `CompiledChartData`

Compiled/pre-sampled chart representation:

- chart-level metadata: `offset`, `chart_end_t`, `playable_count`, `sample_rate`, `t_start`, `sample_count`
- `CompiledLine` arrays for `pos_x`, `pos_y`, `rot`, `alpha`, `scroll`, and optional dynamic color
- `notes` copied as plain note records with baked `t_enter`

`CompiledChartData::to_chart_data()` rehydrates a normal `ChartData` by wrapping sampled arrays in `SampledTrack` lambdas.

## Frame Snapshots

### `LineSnapshot`

Per-frame line evaluation:

- transform: `x`, `y`, `rot`, `cos_rot`, `sin_rot`
- visibility/color: `alpha01`, `scroll`, `color`
- presentation: `incline`, `scale_x`, `scale_y`, `texture_path`, `text`
- ordering: `is_cover`, `z_order`

### `NoteSnapshot`

Per-frame visible note state:

- head/tail world positions
- note alpha, line rotation, size, color
- hold flags, judged/miss flags, multi-hit flag, skew

### `FrameSnapshot`

Top-level CPU frame payload:

- `t`
- `lines`
- `notes`
- `hud`

## Invariants

These assumptions are important across the codebase:

- `ChartData.notes` is sorted by `t_hit` after parsing.
- `playable_count` counts `fake == false` notes only.
- `chart_end_t` is derived from note end times.
- `mh` is assigned during `ChartData::finalize()` for simultaneous playable notes.
- `early_notes` and `notes_by_enter` are sorted by `t_enter` and are used for visibility/candidate selection.
- Compiled charts set `is_compiled = true` so callers skip `precompute_t_enter()`.
