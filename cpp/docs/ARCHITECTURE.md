# Architecture Overview

> 🌐 [中文](ARCHITECTURE.zh.md)

This page documents the current C++ module layout, concrete types, and runtime workflows.

For user workflows, start from [../../docs/CPP_RENDERER.md](../../docs/CPP_RENDERER.md).  
For subsystem detail, continue into the dedicated pages linked at the bottom.

---

## Directory Structure

```text
cpp/
├── CMakeLists.txt
├── include/phigros/
│   ├── api/      Native API surface used by Python bindings
│   ├── app/      CLI, window, input, platform integration
│   ├── chart/    Loaders, parsers, compiler, PHBC I/O
│   ├── config/   RenderConfig load/save/defaults
│   ├── core/     Core types, logging, mods
│   ├── engine/   Judge, autoplay, hold logic, kinematics, visibility
│   ├── hud/      HUD state
│   ├── io/       Audio, replay, respack, video encoder
│   ├── math/     Easing, tracks, math utilities
│   └── render/   Frame snapshots, renderers, executors, targets
├── src/
│   ├── api/      PreparedChart / autoplay / PHBC implementation
│   ├── app/      Native renderer executable entry
│   ├── chart/    Parser/loader/compiler implementations
│   ├── python/   Python extension module glue
│   ├── vendor/   Vendor-backed translation units
│   └── main.cpp  Headless/native core entry
├── tests/        Native tests and benchmark entrypoints
├── vendor/       miniz, miniaudio, stb
├── scripts/      Build helpers and launcher tooling
├── shaders/      bgfx shader sources
├── mods/         Built-in mod examples
├── web/          WASM shell assets
├── android/      Android wrapper project
└── ios/          iOS wrapper project
```

---

## Build Targets

Key targets defined in `cpp/CMakeLists.txt`:

| Target | Description |
|---|---|
| `phigros_core_lib` | Core chart/parser/compiler library (no render deps) |
| `phigros_core` | Headless/native CLI target built on `phigros_core_lib` |
| `phigros_python_api_lib` | Native API layer consumed by the Python extension |
| `phigros_render` | Full renderer/player app (render-app targets must be enabled) |
| `chart_scanner` | Chart-directory discovery utility |
| `test_easing`, `test_engine`, `test_parser`, `test_logger`, `test_zip_extract`, `verify_chart`, `bench` | Unit tests and benchmark entrypoints |

Vendor support libraries: `vendor_stb`, `vendor_miniz`, `vendor_miniaudio`.

---

## Core Types (`core/types.hpp`)

### `NoteKind : int`

```cpp
enum class NoteKind : int { Tap = 1, Drag = 2, Hold = 3, Flick = 4 };
```

### `Note`

| Field | Type | Meaning |
|---|---|---|
| `nid`, `line_id`, `kind` | `int` | Identity and kind (1–4) |
| `above`, `fake` | `bool` | Side of line; excluded from scoring |
| `t_hit`, `t_end` | `double` | Chart-time hit / hold-end in seconds |
| `x_local_px`, `y_offset_px` | `double` | Local position on the judge line |
| `speed_mul`, `size_px`, `alpha01` | `double` | Per-note visual modifiers |
| `tint_rgb`, `tint_hitfx_rgb` | `math::RGB`, `optional<math::RGB>` | Note tint |
| `scroll_hit`, `scroll_end` | `double` | Cached scroll integral at key times |
| `visible_time` | `double` | Seconds before hit when note becomes visible (default 999999 = always) |
| `t_enter` | `double` | Precomputed time note first appears on screen |
| `mh` | `bool` | Multi-hit simultaneous flag |
| `hitsound_path` | `std::string` | RPE custom hitsound |

### `Line`

| Field | Type | Meaning |
|---|---|---|
| `lid` | `int` | Unique line ID |
| `pos_x`, `pos_y`, `rot`, `alpha` | `TrackFn` (= `std::function<double(double)>`) | Core animated tracks |
| `scroll_px` | `math::IntegralTrack` | Scroll position integral |
| `scroll_fn` | `TrackFn` | Compiled scroll override (set by `compile_chart()`) |
| `color_rgb` | `math::RGB` | Static fallback color |
| `color` | `shared_ptr<math::PiecewiseColor>` | RPE dynamic color track |
| `compiled_color` | `ColorFn` (= `std::function<math::RGB(double)>`) | Compiled color override |
| `scale_x`, `scale_y` | `shared_ptr<math::PiecewiseEased>` | RPE scale tracks |
| `text` | `shared_ptr<math::PiecewiseText>` | RPE textEvents |
| `texture_path` | `std::string` | RPE line texture |
| `father`, `rotate_with_father` | `int`, `bool` | Parent-line hierarchy |
| `attach_ui` | `std::string` | RPE UI element binding (hides the line) |
| `z_order`, `is_cover` | `int`, `bool` | RPE draw-order control |
| `incline` | `shared_ptr<math::PiecewiseEased>` | RPE perspective tilt (degrees) |
| `alpha_ctrl`, `pos_ctrl`, `size_ctrl`, `y_ctrl`, `skew_ctrl` | `vector<CtrlPoint>` | RPE per-scroll-distance note modifiers |

### `CtrlPoint`

```cpp
struct CtrlPoint {
    float x;      // note's scroll distance from judge line (RPE y-units)
    float value;  // property value at this distance
    int   easing; // 1 = linear
};
```

Used by `eval_ctrl(pts, x, def)` which linearly interpolates the closest pair around `x`, clamping at the boundary values.

### `NoteState`

Tracks live judge state per note:

| Field | Type | Meaning |
|---|---|---|
| `judged`, `hit`, `miss` | `bool` | Judgment outcome flags |
| `holding`, `released_early`, `hold_failed`, `hold_finalized` | `bool` | Hold lifecycle flags |
| `judge_t`, `judge_delta_ms`, `judge_grade` | `double`, `double`, `string` | Judgment result |
| `hold_grade` | `string` | Initial grade at hold-head hit |
| `release_t` | `double` | Time of early release |

### `ChartData`

| Field | Type | Meaning |
|---|---|---|
| `offset` | `double` | Audio offset in seconds |
| `lines` | `vector<Line>` | All judge lines |
| `notes` | `vector<Note>` | All notes, sorted by `t_hit` |
| `chart_end_t`, `playable_count` | `double`, `int` | Precomputed summary stats |
| `is_compiled` | `bool` | `true` when loaded from PHBC (skips `precompute_t_enter`) |
| `early_notes` | `vector<size_t>` | Indices into `notes[]` sorted by `t_enter`, for notes where `t_hit - t_enter > 15 s` |
| `notes_by_enter` | `vector<size_t>` | Full index of all notes sorted by `t_enter` — used for O(Δ) per-frame culling |
| `meta_song_path`, `meta_bg_path` | `string` | RPE META asset paths |

`ChartData::finalize()` sets `chart_end_t`, `playable_count`, and the `mh` (multi-hit) flags. Must be called once after parsing.

---

## Config Types (`config/render_config.hpp`)

### `RenderConfig`

Selected important fields:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `window_w`, `window_h` | `int` | 1280, 720 | Render resolution |
| `approach` | `double` | 3.0 | Note approach time in seconds; clamped to [0.1, 30] |
| `chart_speed` | `double` | 1.0 | Chart speed multiplier; clamped to [0.1, 20] |
| `playback_speed` | `optional<double>` | nullopt | Overrides chart_speed for audio/time; falls back to chart_speed |
| `expand_factor` | `double` | 1.0 | Lane compress factor (>1 compresses world coords toward centre) |
| `note_scale_x`, `note_scale_y` | `double` | 2.5, 1.0 | Note size multipliers |
| `note_alpha` | `double` | 1.0 | Global note alpha; clamped to [0, 1] |
| `font_size` | `double` | 1.0 | HUD text scale; clamped to [0.5, 3] |
| `line_alpha_mode` | `LineAlphaMode` | `NegativeOnly` | How judge-line alpha modulates note alpha |
| `backend` | `string` | `"sdl3_bgfx"` | Renderer backend string |
| `autoplay` | `bool` | true | Enables `SimulatePlayer` |
| `simulateplay` | `SimulatePlayConfig` | — | SimulatePlay sub-config |
| `audio_offset_ms` | `double` | 0.0 | Positive = advance notes relative to audio |
| `trail_alpha`, `trail_frames`, `trail_decay` | `optional<double/int>` | — | Trail effect parameters |
| `motion_blur_samples`, `motion_blur_shutter` | `optional<int/double>` | — | Motion blur parameters |

`LineAlphaMode` values: `Off`, `NegativeOnly` (default — dims notes when `alpha_raw < 0`), `Always`.

Config is loaded from JSONC via `load_config(path)` → `load_config_text(text)` → `load_config_json(json)`.  
Serialization: `config_to_json(cfg)` → `save_config(path, cfg)`.

---

## Chart Loading Workflow

```
chart path string
        │
        ▼
chart::resolve_chart_entry(path, preferred_difficulty="IN")
        │ returns optional<ChartEntry>
        │   ChartEntry { name, difficulty, chart_path,
        │                ChartAssets { music_path, illustration_path },
        │                source_type ("folder"|"zip"|"json") }
        ▼
Detect format from extension:
  .phbc  → chart::read_phbc(istream, password)
               → CompiledChartData → .to_chart_data() → ChartData
  .json  → nlohmann::json parse
               → chart::parse_rpe(j, W, H, shift)    (RPE 2.x)
               → chart::parse_official(j, W, H)       (official .0 format)
               → ChartData
  .pec   → chart::parse_pec(path, W, H)               (PEC text format)
               → ChartData
  zip/pez → chart::extract_zip_file(zip_path, file_in_zip)
               → bytes → parse as above
        │
        ▼
ChartData::finalize()          — sets chart_end_t, playable_count, mh flags
ChartData::build_early_notes_index()    — notes where t_hit−t_enter > 15 s
ChartData::build_notes_by_enter_index() — full O(Δ) culling index (sorted by t_enter)
        │
        ▼  (when --compile is requested)
chart::compile_chart(ChartData, sample_rate=240 Hz)
        → CompiledChartData { offset, chart_end_t, playable_count,
                              sample_rate, t_start, sample_count,
                              vector<CompiledLine> { lid, color_rgb,
                                  vector<float> pos_x/y/rot/alpha/scroll,
                                  optional color_r/g/b arrays },
                              vector<Note> }
chart::write_phbc(CompiledChartData, ostream, PhbcWriteOptions)
```

### PHBC Binary Format (v2 header: 52 bytes)

```
[0-3]   magic "PHBC"
[4-5]   uint16_t version (1 or 2)
[6-7]   uint16_t flags — bit0=compressed, bit1=LZMA, bit2=encrypted, bits3-4=enc_algo
[8-47]  double offset, chart_end_t; int32 playable_count, note_count,
        line_count; float sample_rate; double t_start; int32 sample_count
v2 metadata blocks (when flags set):
  if compressed: uint32_t uncompressed_size
  if encrypted:  uint8_t salt[16], iv[16], tag[16]
Payload per-line:
  int32 lid; uint8 color_r/g/b/_pad; int32 dyn_color
  [if dyn_color] sample_count × float color_r/g/b
  sample_count × float pos_x/pos_y/rot/alpha/scroll
Payload per-note:
  int32 nid/line_id/kind; uint8 above/fake/mh/_pad
  double t_hit/t_end/t_enter/scroll_hit/scroll_end
  double x_local_px/y_offset_px; float speed_mul/size_px/alpha01
  uint8 tint_r/g/b/_pad
```

Encryption algos: `AES-256-GCM (0)`, `AES-256-CBC (1)`, `ChaCha20-Poly1305 (2)`, `XOR (3)`.  
Compression algos: `zlib (0)`, `LZMA (1)`.

---

## Runtime Data Flow

```
AppArgs (parsed CLI)
  + config::RenderConfig (load_config / CLI overrides applied on top)
        │
        ▼
AppContext::init(chart_path, chart_offset, respack_override,
                 bg_override, font_path, audio_override,
                 headless, W, H, cfg)
   ┌────────────────────────────────────────────────┐
   │  Window::init(W, H, title, headless, vsync,    │
   │               backend)                          │
   │  render::SpriteBatch::init(ren)                 │
   │  render::DrawList::reserve(2048)                │
   │  io::load_respack(ren, respack_path)            │
   │    → io::Respack { RespackConfig, Textures,     │
   │                    hitsound_ogg[1..4] }          │
   │  BackgroundRenderer::load(…, bg_blur)           │
   │  NoteRenderer::init(W, H, scale_x, scale_y)    │
   │  HoldRenderer::init(…)                          │
   │  TrailRenderer::init(…, cfg)                    │
   │  MotionBlurRenderer::init(…, cfg)               │
   │  HudRenderer::init(ren, font, W, H, …)         │
   │  io::AudioSystem::init() + load_bgm(path, off) │
   │  io::AudioSystem::load_hitsound(kind, ogg)     │
   └────────────────────────────────────────────────┘
        │
        ▼
ChartData load (see Chart Loading Workflow above)
        │
        ▼
GameLoop::GameLoop(ctx, args, cfg, chart, playable_notes, chart_end)
   ┌────────────────────────────────────────────────┐
   │  vector<NoteState> states (one per note)        │
   │  engine::Judge judge                            │
   │  engine::EffectManager effects                  │
   │  engine::SimulatePlayer autoplay (Conservative) │
   │  engine::ScriptPlayPlayer scriptplay            │
   │  engine::ManualJudge manual_judge               │
   │  io::ReplayWriter replay_writer                 │
   │  io::ReplayPlayer replay_player                 │
   │  io::RecordingSession recorder                  │
   └────────────────────────────────────────────────┘
        │
        ▼
GameLoop::run_frame() loop  ───────────────────────────────────────────┐
  1. Advance chart time t from audio position (or SDL_GetTicks)         │
  2. engine::SimulatePlayer::step(t, notes, states, lines, judge, W, H)│
     OR engine::ManualJudge::update(t, …) for play mode                │
     OR engine::ScriptPlayPlayer::step(…) for scriptplay               │
  3. engine::hold_logic_step(t, notes, states, judge)                  │
  4. engine::NoteManager::tick(t, notes, states, judge)  (auto-miss)   │
  5. render::build_frame(t, chart, states, judge, cfg)                 │
     → engine::eval_line_state(line, t) × N_lines                      │
       → LineState { x, y, rot, alpha01, scroll, alpha_raw,            │
                     cos_rot, sin_rot }                                 │
     → engine::note_world_pos_cs(…) per visible note                   │
       → eval_ctrl(alpha/pos/size/y/skew, scroll_dist)                 │
       → NoteSnapshot { nid, kind, wx/wy, wx_tail/wy_tail,             │
                        alpha, line_rot, size_px, color,               │
                        is_hold, judged, miss, mh,                     │
                        holding, skew }                                 │
     → engine::compute_score(acc_sum, max_combo, playable_count)       │
       → ScoreResult { score, acc_ratio, combo_ratio }                 │
     → hud::update_hud(frame.hud, score, acc, combo, …)               │
     → FrameSnapshot { t, vector<LineSnapshot>, vector<NoteSnapshot>,  │
                       hud::HudState }                                  │
  6. render::Renderer::draw_frame(frame, ctx)                          │
     BackgroundRenderer → LineRenderer → NoteRenderer                  │
     → HoldRenderer → HitFXRenderer → TrailRenderer                   │
     → MotionBlurRenderer → HudRenderer                                │
  7. SDL_RenderPresent / bgfx::frame                                   │
  8. (optional) io::RecordingSession::capture_frame → video encoder    │
  9. (optional) screenshot to disk on --screenshot-fps interval        │
  └──────────────────────────────────────────────────────────────────── (loop)
```

---

## Kinematics: `engine::eval_line_state` and `note_world_pos_cs`

**`eval_line_state(line, t) → LineState`**:
- Evaluates `line.pos_x(t)`, `line.pos_y(t)`, `line.rot(t)`, `line.alpha(t)`.
- Scroll: uses `line.scroll_fn(t)` if compiled, else `line.scroll_px.integral(t)`.
- Precomputes `cos_rot`, `sin_rot` to amortise trig over all notes on the line.
- Respects `force_line_alpha01` and `force_line_alpha01_by_lid` overrides.

**`note_world_pos_cs(line_x, line_y, cos_r, sin_r, scroll_now, note, scroll_target, for_tail, flow_mul, speed_mul_affects_travel, hold_keep_head) → Vec2`**:
- Tangent direction `(cos_r, sin_r)`, normal `(-sin_r, cos_r)`.
- `dy = (scroll_target − scroll_now) × flow_mul`
- Hold tail: `dy` scaled by `note.speed_mul`. Non-hold when `speed_mul_affects_travel`: also scaled.
- `hold_keep_head`: clamps `dy ≥ 0` so the head never passes through the line.
- Final position: `{line_x + cos_r × x_local + (−sin_r) × y_local, line_y + sin_r × x_local + cos_r × y_local}` where `y_local = sgn × dy × mult + note.y_offset_px`.

---

## Scoring / Judge (`engine/judge.hpp`)

### Judge windows (seconds from ideal hit)

| Grade | Window |
|---|---|
| PERFECT | ≤ 0.045 s |
| GOOD | ≤ 0.090 s |
| BAD | ≤ 0.150 s |
| MISS | > 0.150 s |

### Score formula

```
acc_ratio  = acc_sum / playable_count        (acc_sum: PERFECT=1.0, GOOD=0.6, others=0)
combo_ratio = max_combo / playable_count
score       = int(acc_ratio × 900000 + combo_ratio × 100000)   → max 1000000
```

### Hold lifecycle

`Judge::start_hold(ns, t)` → grades head timing, sets `ns.hit`, `ns.holding`, `ns.hold_grade`.  
`Judge::finalize_hold(ns)` → called at `t ≥ t_end` or early release; commits grade to `acc_sum`.  
`ns.hold_failed` set by hold-logic when the tail is released early with `release_t < t_end × hold_tail_tol`.

---

## SimulatePlayer (`engine/simulateplay.hpp`)

Three simulation modes:

| Mode | Timing jitter |
|---|---|
| `Conservative` | amp × 0.6 |
| `Aggressive` | amp × 1.0 |
| `Extreme` | amp × 1.6 |

Key data types:
- `PointerState` — per-finger position, target, `holding_note`, flick/fade timers.
- `NotePlan` — pre-scheduled judgment: `judge_t`, world `(x, y)`, `pointer_idx`.
- `SimPointerVisual` — exported for rendering: position, `fade_alpha`, trail samples.
- `SimHitEvent` — emitted per hit: `note_idx`, `judge_t`, `delta_ms`, grade.

`SimulatePlayer::step(t, notes, states, lines, judge, W, H)`:
1. `release_finished_taps(t)` — clear expired tap pointers.
2. `release_completed_holds(t, …)` — finalize holds past `t_end`.
3. `plan_note(i, n, …, t)` — schedule each upcoming note within `lookahead_s(kind)` (50–120 ms) on a free pointer via `choose_pointer(…)`.
4. Fire ready plans: call `judge.start_hold` or `judge.try_hit`.
5. `update_pointer_motion(t)` — lerp pointers toward targets (max_speed = 2600 px/s when humanized).
6. `record_visual_trails(t)` — append trail samples, prune by `trail_seconds_`.

---

## Frame Snapshot and Culling

`render::build_frame(t, chart, states, judge, cfg) → FrameSnapshot`:

1. **Line pass**: evaluates all lines with `eval_line_state`; stores 256 line states on the stack (heap fallback for larger charts). Sorts by `z_order` (stable sort; fast-paths all-same-z case).
2. **Note culling** — four layers:
   - `visible_time` gate (`t < t_hit − visible_time` → skip).
   - `t_enter` / `t_end` gate (disabled by `no_cull_enter_time`, default disabled).
   - Negative-scroll ghost filter (cull notes whose scroll has pre-run past `scroll_hit` but `t < t_hit`).
   - Screen-bounds test with expand transform (disabled by `no_cull_screen`).
3. **Watermark optimisation**: thread-local `EnterWM` tracks the lowest active index into `notes_by_enter`, advancing past fully-expired notes. Backward seeks reset to 0; forward playback pays O(Δ notes) per frame.
4. **Control events**: `eval_ctrl(alpha_ctrl/pos_ctrl/size_ctrl/y_ctrl/skew_ctrl, scroll_dist_rpe)` for each visible note.
5. **Counting-sort** by `(is_hold, kind)` bucket to minimise texture-state changes in `SdlExecutor` (8 buckets max, O(N)).

Resulting types:
- `LineSnapshot { lid, x, y, rot, cos_rot, sin_rot, alpha01, scroll, color, incline, is_cover, z_order, scale_x, scale_y, texture_path*, text }`
- `NoteSnapshot { nid, kind, wx, wy, wx_tail, wy_tail, alpha, line_rot, size_px, color, is_hold, judged, miss, mh, holding, draw_hold_head, hold_hit_failed, skew }`
- `FrameSnapshot { t, vector<LineSnapshot>, vector<NoteSnapshot>, hud::HudState }`

---

## Respack (`io/respack.hpp`)

`Respack` is loaded with `io::load_respack(SDL_Renderer*, zip_path)`:
- Parses `info.yml` → `RespackConfig { hitfx_cols/rows, hold_head/tail_h, hitfx_duration/scale/rotate/tinted, holdKeepHead, holdRepeat, holdCompact, colorPerfect/Good }`.
- Textures: `click`, `drag`, `flick`, `hold` (+ `_mh` multi-hit variants), `hitfx_sheet`.
- Hitsound OGG bytes: `hitsound_ogg[1..4]` (index 3 = hold, optional).
- Fallback: if ZIP fails to open, solid-color placeholder textures are created so rendering always works.

`Respack::note_texture(kind, mh_flag)` — selects the correct texture, falling back to normal variant when `_mh` is absent.

---

## AppContext Members

```
AppContext {
  Window                window        — SDL window + renderer (or bgfx device)
  render::SpriteBatch   batch         — 2D quad batcher
  render::DrawList      draw_list     — deferred command list (capacity 2048)
  io::Respack           respack       — note textures + hitsounds
  BackgroundRenderer    bg            — blurred/dimmed background image
  LineRenderer          line_ren      — judge-line draw; line_w=H×0.005, dot_r=H×0.007
  NoteRenderer          note_ren      — tap/drag/flick sprites
  HoldRenderer          hold_ren      — hold body + head/tail splice
  HitFXRenderer         hitfx_ren     — spritesheet hit effects + particles
  HudRenderer           hud_ren       — score, combo, accuracy, progress bar
  TrailRenderer         trail         — frame-echo trail (optional)
  MotionBlurRenderer    motion_blur   — multi-sample motion blur (optional)
  InputManager          input         — pointer/keyboard events
  io::AudioSystem       audio         — miniaudio BGM + hitsound pools
  unordered_map<string, Texture> line_tex_cache  — RPE line texture cache (zip-aware)
}
```

Audio source resolution order: CLI `--audio` override → RPE META `meta_song_path` → respack embedded → `find_chart_audio(chart_dir)` (scans for `music.ogg/mp3/wav`, `bgm.*`, then any `*.ogg/.mp3/.wav/.flac`).

---

## Module Responsibilities

| Module | Key types / functions |
|---|---|
| `core/` | `Note`, `Line`, `NoteState`, `ChartData`, `TrackFn`, `ColorFn`, `CtrlPoint`, `eval_ctrl()` |
| `chart/` | `ChartEntry`, `ChartAssets`, `CompiledChartData`, `SampledTrack`, `parse_official/rpe/pec()`, `compile_chart()`, `write_phbc/read_phbc()`, `scan_charts_directory()`, `resolve_chart_entry()` |
| `math/` | `PiecewiseEased`, `PiecewiseColor`, `PiecewiseText`, `IntegralTrack`, easing library |
| `engine/` | `LineState`, `eval_line_state()`, `note_world_pos_cs()`, `Judge`, `SimulatePlayer`, `ManualJudge`, `ScriptPlayPlayer`, `NoteManager`, `EffectManager`, `HoldLogic` |
| `render/` | `FrameSnapshot`, `LineSnapshot`, `NoteSnapshot`, `build_frame()`, `SpriteBatch`, `DrawList`, renderer classes, `Texture` |
| `config/` | `RenderConfig`, `LineAlphaMode`, `load_config/config_to_json/save_config()` |
| `api/` | `PreparedChart`, autoplay helper — stable native surface for Python bindings |
| `app/` | `AppContext`, `AppArgs`, `GameLoop`, `Window`, `InputManager`, platform wiring |
| `io/` | `Respack`, `RespackConfig`, `AudioSystem`, `ReplayWriter/Player`, `RecordingSession`, `VideoEncoder` |

---

## Internal Doc Map

- [INTERFACES.md](INTERFACES.md)
- [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
- [MATH.md](MATH.md)
- [FORMAT.md](FORMAT.md)
- [KINEMATICS.md](KINEMATICS.md)
- [RENDER.md](RENDER.md)
- [CONFIG.md](CONFIG.md)
- [BUILD_AND_TEST.md](BUILD_AND_TEST.md)
- [CHART_LOADER.md](CHART_LOADER.md)
- [DEBUG_FLAGS.md](DEBUG_FLAGS.md)
