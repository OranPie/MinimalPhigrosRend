# Render

> 🌐 [中文](RENDER.zh.md)

The render layer is split into two parts:

- CPU-side frame construction in `render/renderer.hpp`
- backend-specific drawing helpers and executors under `render/`

## Frame Construction

`build_frame()` takes:

- time `t`
- `ChartData`
- per-note `NoteState` array
- `engine::Judge`
- `RenderConfig`

It returns a `FrameSnapshot` with evaluated `LineSnapshot`, `NoteSnapshot`, and HUD state.

## High-Level Pipeline

```text
line evaluation
  -> line sorting by z-order
  -> note candidate filtering
  -> world-position evaluation
  -> render-time control-event adjustments
  -> note alpha / culling decisions
  -> HUD snapshot
  -> backend draw path
```

## Snapshot Responsibilities

- `LineSnapshot`: transform, alpha, scroll, color, line text/texture, cover/z-order state
- `NoteSnapshot`: evaluated head/tail positions, alpha, color, size, hold flags, miss/judge flags
- `FrameSnapshot`: top-level payload used by native rendering and Python-side inspection

## Draw Subsystems

Major render helpers under `include/phigros/render/`:

- `background.hpp`
- `line_renderer.hpp`
- `note_renderer.hpp`
- `hold_renderer.hpp`
- `hitfx_renderer.hpp`
- `hud_renderer.hpp`
- `pause_overlay.hpp`
- `result_screen.hpp`
- `trail_renderer.hpp`
- `motion_blur.hpp`
- executor/backends such as `sdl_renderer.hpp`, `sdl_executor.hpp`, `bgfx_renderer.hpp`, `bgfx_executor.hpp`

## Visibility and Alpha Behavior

Important render-time behavior includes:

- optional `t_enter` culling for dense charts
- screen-space culling after expand transform
- hold visibility checks against both head and tail
- note alpha modulation from note-local alpha, control events, global config, and line-alpha mode
- cover/z-order sorting on lines before draw submission

## Performance Notes

Hot-path decisions already visible in the code:

- stack storage for common line counts
- precomputed line trig values reused across notes
- adaptive vector reserve for note snapshots
- compiled sampled tracks to reduce repeated piecewise evaluation cost

## Related Docs

- [KINEMATICS.md](KINEMATICS.md)
- [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
- [CONFIG.md](CONFIG.md)
