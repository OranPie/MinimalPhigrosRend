# Architecture Overview

> 🌐 [中文](ARCHITECTURE.zh.md)
```
cpp/
├── include/phigros/       # All headers (header-only core)
│   ├── chart/             # Chart format parsers
│   ├── math/              # Easing, tracks, geometry
│   ├── core/              # Shared types
│   ├── engine/            # Chart simulation engine
│   ├── config/            # Config loading/saving
│   ├── hud/               # HUD state
│   ├── render/            # Frame building + rendering
│   ├── io/                # Respack, replay, video export
│   └── app/               # Application layer (game loop, input)
├── src/
│   └── app/main.cpp       # CLI entry point (~200 lines)
├── tests/
│   ├── test_engine.cpp    # Unit tests (6343 checks)
│   └── test_parser.cpp    # Parser tests (54 checks)
└── vendor/                # nlohmann/json, miniz, stb, miniaudio
```

---

## Data Flow

```
CLI args + config.jsonc
        │
        ▼
   RenderConfig
        │
        ▼
  Chart parser  ──►  ChartData  { lines[], notes[] }
                          │
              ┌───────────┼────────────┐
              ▼           ▼            ▼
       NoteManager   SimulatePlayer  ManualJudge
       (visibility)  (autoplay)      (--play mode)
              │           │               ▲
              │           │               │
              │           │         JudgeInputFrame
              │           │         (from InputManager)
              └─────┬─────┘
                    ▼
              engine::Judge
              (acc_sum, combo)
                    │
                    ▼
           build_frame(t, ...)
                    │
                    ▼
             FrameSnapshot  { lines[], notes[], hud }
                    │
              ┌─────┼─────┐
              ▼     ▼     ▼
         LineRen  NoteRen  HoldRen  HitFXRen  HudRen
              │
              ▼
          SpriteBatch  (SDL draw calls / bgfx executor)
              │
              ▼
           Window
```

---

## Module Reference

### `chart/` — Parsers

| File | Description |
|------|-------------|
| `official_parser.hpp` | Parses Phigros Official JSON format |
| `rpe_parser.hpp` | Parses RPE (Re:PhiEdit) JSON format |
| `pec_parser.hpp` | Parses PEC binary/text format |
| `chart_loader.hpp` | Auto-detects format, calls the right parser |
| `bpm_map.hpp` | `BpmMap` — beat-to-seconds conversion (BPM segment list) |
| `compiled_chart.hpp` | `CompiledChartData` — pre-sampled flat float arrays for all line tracks |
| `compiler.hpp` | `compile_chart()` — samples all easing tracks at a fixed Hz into `CompiledChartData` |
| `phbc_io.hpp` | `write_phbc()` / `read_phbc()` — binary `.phbc` round-trip serialization |
| `sampled_track.hpp` | `SampledTrack` — O(1) lerp lookup into a uniform float sample array |

All parsers produce `ChartData`:
```cpp
struct ChartData {
    std::vector<Line>   lines;
    std::vector<Note>   notes;  // sorted by t_hit
    ChartMeta           meta;
};
```

Notes are **sorted by `t_hit`** after parsing — required for binary-search visibility bounds.

### `math/` — Math primitives

| File | Description |
|------|-------------|
| `easing.hpp` | 29 easing functions (matching RPE index) |
| `tracks.hpp` | `ConstTrack`, `LinearTrack`, `EasedTrack`, `IntegralTrack` — piecewise evaluated with binary-search `seek()` |
| `geometry.hpp` | `note_world_pos()` — converts chart-space note offset to screen-space XY |

`IntegralTrack` is used for `scroll` (the integral of `speed_event * chart_speed`) and is the key to correct note flow positioning.

### `engine/` — Simulation

| File | Description |
|------|-------------|
| `kinematics.hpp` | `LineState`, `eval_line_state()`, `note_world_pos_cs()` (precomputed cos/sin) |
| `judge.hpp` | `Judge` — timing windows (PERFECT=45ms, GOOD=90ms, BAD=150ms), `compute_score()` |
| `judge_input.hpp` | `JudgeAction`, `JudgeInputFrame` — platform-agnostic input abstraction for the judge |
| `note_manager.hpp` | `precompute_t_enter()`, binary-search visibility update |
| `hold_logic.hpp` | `hold_maintenance()`, `hold_finalize()`, `detect_misses()` |
| `simulateplay.hpp` | `SimulatePlayer` — frame-accurate autoplay simulation |
| `manual_judge.hpp` | `ManualJudge` — spatial+temporal note matching for interactive play; consumes `JudgeInputFrame` (pointer: spatial+temporal, keyboard: temporal-only) |
| `effects.hpp` | `EffectManager` — `HitFX`, `FlashFX`, `ParticleBurst` lifecycle |
| `visibility.hpp` | `scroll_speed_at()`, AABB visibility check helpers used by `note_manager` |

#### Judge timing constants

| Grade | Window (±ms) | acc weight |
|-------|-------------|------------|
| PERFECT | 45 | 1.0 |
| GOOD | 90 | 0.6 |
| BAD | 150 | 0.0 |
| MISS | > 150 | 0.0 |

#### Score formula

```
score = int( acc_sum/N × 900000 + max_combo/N × 100000 )
```

### `render/` — Frame building and drawing

| File | Description |
|------|-------------|
| `renderer.hpp` | `build_frame()` — evaluates all line/note states into `FrameSnapshot` |
| `note_renderer.hpp` | Draws tap/drag/flick notes; applies `size_px`, outline, miss fade |
| `hold_renderer.hpp` | Draws hold notes (head/body/tail slices); hold-glow overlay |
| `line_renderer.hpp` | Draws judge lines as rotated rectangles + center dot |
| `hitfx_renderer.hpp` | Draws `HitFX` (rotating sprite / animated ring) + `ParticleBurst` |
| `hud_renderer.hpp` | Draws score, combo, accuracy, progress bar via `FontAtlas` |
| `bg_renderer.hpp` | Blurred background + dim overlay |
| `result_screen.hpp` | End-of-chart result overlay (score, grade, accuracy, combo) |
| `pause_overlay.hpp` | Pause state semi-transparent overlay + hint text |
| `trail_renderer.hpp` | Circular buffer of `RenderTarget` slots for trail/ghost effect |
| `motion_blur.hpp` | Sub-frame accumulation for motion blur |
| `render_target.hpp` | `SDL_TEXTUREACCESS_TARGET` wrapper |

#### `build_frame()` optimisations

1. **Binary-search note window**: only iterates notes in `[t − 12s, t + approach×2]`
2. **Stack line array**: `std::array<LineState, 256>` (no heap alloc)
3. **Precomputed trig**: `LineState::cos_rot / sin_rot` computed once per line; `note_world_pos_cs()` skips `cos()/sin()` per note
4. **Adaptive reserve**: `thread_local` last-frame note count hint for `vector::reserve()`

### `config/` — Configuration

`render_config.hpp` contains `RenderConfig` (plain struct), `load_config()` (JSONC parse + clamping), `config_to_json()`, and `save_config()`.

`LineAlphaMode` enum avoids string comparisons in the hot render path.

### `io/` — I/O

| File | Description |
|------|-------------|
| `respack.hpp` | Loads `respack.zip` via miniz; holds note/hitfx textures |
| `replay.hpp` | `ReplayWriter` (records events) + `ReplayPlayer` (replays); miniz-compressed binary format |
| `video_encoder.hpp` | `RecordingSession` — pipes raw RGBA frames to FFmpeg subprocess |
| `audio.hpp` | `AudioSystem` — miniaudio-based BGM player with offset compensation |

#### Replay format

```
Header:  magic[4]  version[2]  chart_hash[4]  unix_ts[8]
Events:  [t:f32  ptr_id:u8  event:u8  x:f16  y:f16] × N
Footer:  crc32[4]
```
Compressed with miniz deflate.

### `app/` — Application layer

| File | Description |
|------|-------------|
| `app_context.hpp` | `AppContext` — owns all long-lived SDL objects, renderers, audio |
| `app_args.hpp` | CLI argument parsing |
| `game_loop.hpp` | `GameLoop::run_frame()` — per-frame update/render driver |
| `input_manager.hpp` | `InputManager` — flat `PointerSlot[10]` array + `KeyAction[10]` keyboard state; mouse, touch, and keyboard input; flick detection; `to_judge_input()` bridge to `JudgeInputFrame` |

#### `GameLoop::run_frame()` phases

1. Timing + event polling
2. Pause check → frozen render
3. Advance time (audio cursor / headless SIM_DT)
4. Exit condition checks
5. Engine update (autoplay or manual judge + hold logic + effects)
6. Skip intermediate sim ticks (headless multi-step)
7. `build_frame()`
8. Render (trail / motion blur / plain path)
9. HUD + result overlay
10. Video capture / screenshot

---

## Effect System

### `HitFX`

Spawned by `EffectManager::add_hitfx()` on every judged note.  
- If respack has a sprite sheet: draws animated frame strip at note position
- Otherwise: draws an expanding ring (`draw_circle_outline()`, 14 segments)
- Animated rotation: `rot += rot_speed × age`
- Non-linear alpha fade: `pow(1 − progress, 0.65)` — fast initial flash, gradual trail

### `FlashFX`

Spawned alongside every `HitFX`. Brief expanding ring (`r: 12 → 70px` over 0.18s).  
Only rendered when no respack sheet is present (matches Python fallback).

### `ParticleBurst`

Spawned by `add_particle_burst()`.  
Each particle: rotated quad, cubic size curve, deceleration `v*(9t/(8t+1))/2`.  
Rendered with `SDL_BLENDMODE_ADD` for glow effect; buffer reused via `get_particles_inplace()`.

---

## Cross-platform Notes

### WASM

- SDL2 + Emscripten (`-DUSE_SDL3=OFF`)
- `emscripten_set_main_loop_arg` replaces desktop while-loop
- `TrailRenderer` and `MotionBlurRenderer` disabled when `__EMSCRIPTEN__` (no `SDL_TEXTUREACCESS_TARGET` on WebGL)
- Assets bundled via `--preload-file respack.zip`

### bgfx backend

- Enabled with `-DUSE_BGFX=ON --backend bgfx`
- `BgfxExecutor` batches `DrawList` commands into transient VB/IB
- Pre-compiled shaders in `shaders/compiled/` for glsl/spirv/metal/dx11/essl

### Mobile

- Touch events → `InputManager` slots 1–9
- CMake mobile toolchain files in `cmake/`
