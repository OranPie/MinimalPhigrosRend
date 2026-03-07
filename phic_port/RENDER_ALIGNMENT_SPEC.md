# Render Alignment Spec: Python ↔ C++

> **Purpose:** Precise reference for every divergence between `phic_renderer` (Python)
> and `phic_port` (C++) that affects visual rendering output and HitFX placement.
> For each metric: Python formula → current C++ behaviour → alignment target.
>
> **Source files:**
> - Python: `phic_renderer/engine/kinematics.py`, `backends/pygame/rendering/frame_renderer.py`,
>   `engine/effects.py`, `backends/pygame/effects/hitfx.py`, `engine/visibility.py`
> - C++: `phic_port/core/src/engine.cpp`, `phic_port/core/src/sw_renderer.cpp`,
>   `phic_port/core/include/phic/core/types.hpp`

---

## 1. Coordinate System Reference

### Python world-coordinate system
| Symbol | Meaning | Units |
|--------|---------|-------|
| `W`, `H` | render width / height | px |
| `lx`, `ly` | judge line position | px (screen space) |
| `rot` | judge line rotation | radians, CCW-positive |
| `sc_now` | `IntegralTrack.integral(t)` | px (cumulative scroll) |
| `scroll_hit` | `IntegralTrack.integral(t_hit)` | px (cached at parse time) |
| `scroll_end` | `IntegralTrack.integral(t_end)` | px (cached, hold tail) |
| `x_local_px` | note offset along line tangent | px |
| `dy` | note offset along line normal | px |
| `above` | which side of the line | bool |

### C++ coordinate system (current — **misaligned**)
| Symbol | Meaning | Units |
|--------|---------|-------|
| `W`, `H` | render width / height | px |
| `lane` | integer bucket (0..lane_count-1) | integer |
| `dt` | `t_hit - now_sec` | seconds |
| `approach_sec` | note approach window | seconds |
| `x` | `(lane+0.5)/lane_count` | normalised 0..1 |
| `y` | `1.0 - dt/approach_sec` | normalised 0..1 |

### Normalised-to-screen mapping (C++ LineAnim — correct)
```
screen_x = eval_norm_x(t) * W   // (pos_x + 675) / 1350 * W
screen_y = eval_norm_y(t) * H   // (450 - pos_y) / 900 * H
```
These are **already correct** in C++; they are just not used for note positioning.

---

## 2. Critical Structural Gap

The C++ engine discards judge-line state when building `FrameCommand` objects.
Notes are placed on a simple grid independent of any line position, rotation, or scroll.

```
Python:   note_pos = f(line_pos, line_rot, line_scroll, note_x_local, note_scroll_hit)
C++:      note_pos = f(lane_index, dt, approach_sec)   // line state ignored
```

This breaks **all** of: note position, hold position, HitFX position, particle position.


---

## 3. Metric 1 — Note World Position (Tap / Drag / Flick)

### Python (`frame_renderer.py` lines 563-573, `kinematics.py` lines 84-118)
```python
# 1. Evaluate line state at t_draw
lx, ly, rot, alpha01, sc_now, _ = eval_line_state(line, t_draw)

# 2. Tangent & normal unit vectors
tx, ty = math.cos(rot), math.sin(rot)      # along-line direction
nx, ny = -math.sin(rot), math.cos(rot)     # perpendicular (normal)

# 3. Distance from line (scroll delta)
dy = (note.scroll_hit - sc_now) * flow_multiplier
if speed_mul_affects_travel:
    dy *= note.speed_mul

# 4. Side sign (+1 above, -1 below)
sgn = 1.0 if note.above else -1.0
y_local = sgn * dy + note.y_offset_px

# 5. World position
x = lx + tx * note.x_local_px + nx * y_local
y = ly + ty * note.x_local_px + ny * y_local
```

### C++ (`engine.cpp` lines 296-310)
```cpp
// note.lane is an integer bucket
const double lane_pos = (note.lane + 0.5) / lane_count;
const double y_raw    = 1.0 - (dt / approach);
const double x_exp    = 0.5 + (lane_pos - 0.5) / expand;
const double y_exp    = 0.5 + (y_raw  - 0.5)  / expand;
// → x and y are normalised [0..1], no line rotation applied
```

### Gap
- No line-rotation-aware positioning
- `x_local_px` not in `RuntimeNote` → lane bucket used instead
- `scroll_hit` not in `RuntimeNote` → time delta used instead
- `above` not in `RuntimeNote` → no side discrimination

### Alignment Target
```cpp
// In build_frame_commands(), after evaluating LineAnim at now_sec_:
const double lx  = line.anim.eval_norm_x(now_sec_) * W;
const double ly  = line.anim.eval_norm_y(now_sec_) * H;
const double rot = line.anim.eval_rot(now_sec_);
const double sc  = line.anim.scroll_px.integral(now_sec_);
const double tx  = std::cos(rot), ty = std::sin(rot);
const double nx  = -ty,           ny = tx;
const double dy  = (note.scroll_hit - sc) * flow_mul;
const double sgn = note.above ? 1.0 : -1.0;
const double xl  = note.x_local_px;
const double yl  = sgn * dy + note.y_offset_px;
cmd.world_x = lx + tx * xl + nx * yl;   // px
cmd.world_y = ly + ty * xl + ny * yl;   // px
```


---

## 4. Metric 2 — Hold Note Head & Tail Position + Progress

### Python (`frame_renderer.py` lines 450-532)
```python
# HEAD: after hit, head is clamped at the line (dy ≥ 0)
if s.hit or s.holding or t_draw >= n.t_hit:
    head_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
else:
    head_scroll = n.scroll_hit
dy_head = (head_scroll - sc_now) * flow_mul
# if hold_keep_head and dy_head < 0: dy_head = 0

# TAIL:
dy_tail = (n.scroll_end - sc_now) * flow_mul
mult    = max(0.0, n.speed_mul)          # Official: speed_mul applied to tail only
y_local_head = sgn * dy_head + n.y_offset_px
y_local_tail = sgn * dy_tail * mult + n.y_offset_px
head = (lx + tx*xl + nx*y_local_head,  ly + ty*xl + ny*y_local_head)
tail = (lx + tx*xl + nx*y_local_tail,  ly + ty*xl + ny*y_local_tail)

# PROGRESS (how much of hold is consumed):
den = n.scroll_end - n.scroll_hit
if abs(den) > 1e-6:
    prog = clamp((sc_now - n.scroll_hit) / den, 0, 1)
else:
    dur = n.t_end - n.t_hit
    prog = clamp((t_draw - n.t_hit) / dur, 0, 1)
```

### C++ (current)
- `hold_end` is passed in `FrameCommand` but `sw_renderer` does not draw a hold body.
- No head/tail split, no progress, no clamping.
- `SwRenderer::draw_notes()` draws holds as a single rectangle at the note lane position.

### Alignment Target
- Add `scroll_hit`, `scroll_end`, `speed_mul`, `above`, `x_local_px`, `y_offset_px` to `RuntimeNote`.
- `FrameCommand` extended with `head_world_x/y`, `tail_world_x/y`, `hold_progress` (0..1).
- SwRenderer draws hold body between head and tail using 3-slice or equivalent.
- Head clamping rule: once `sc_now > scroll_hit`, head stays at line (`dy_head = 0`).

---

## 5. Metric 3 — HitFX Spawn Position (on Judgment)

### Python (`pygame_backend.py` lines 2022-2034)
```python
# Evaluated at t_fx = judgment time (not render time)
lx, ly, lr, la, sc, _ = eval_line_state(line, t_fx)
x, y = note_world_pos(lx, ly, lr, sc, note, note.scroll_hit, for_tail=False)
# note: scroll_target = note.scroll_hit regardless of sc (pre-hit position)
hitfx.append(HitFX(x, y, t_fx, color, lr, variant))
```
Key point: `note_world_pos` is called with `scroll_target = note.scroll_hit`, NOT `sc`.
This places the HitFX exactly at the note's hit position on the line.

### C++ (`sw_renderer.cpp` lines 602-636)
```cpp
// Looks up FrameCommand for the note — but FrameCommand uses lane-grid pos
for (const auto& cmd : cmds) {
    if (cmd.note_id == ev.note_id) {
        fx_x = cmd.x;   // <- wrong: lane-grid x, not world x at t_fx
        fx_y = cmd.y;   // <- wrong
        break;
    }
}
// Fallback: (lane+0.5)/lane_count, fx_y=0.5  ← even more wrong
```

### Gap
HitFX position is taken from the wrong coordinate frame. Even if FrameCommand is fixed,
position must be evaluated at `t_fx` (judgment time), not `t_draw` (current render time).

### Alignment Target
```cpp
// At judgment time, store snapshot:
struct JudgeFxSnapshot {
    double world_x, world_y;  // evaluated via note_world_pos at t_fx
    double rot;                // line rotation at t_fx (for hitfx_rotate)
};
// Then in push_judge_events / step(), resolve snapshots from the engine's
// line-state eval at the exact judgment timestamp.
```


---

## 6. Metric 4 — Periodic Hold HitFX Position

### Python (`backends/pygame/hold/logic.py` lines 237-256)
```python
# Fires every hold_fx_interval_ms while note.holding == True
lx, ly, lr, _, sc_now, _ = eval_line_state(line, t)
# scroll_target = sc_now (head is ON the line at this point)
x, y = note_world_pos(lx, ly, lr, sc_now, note, sc_now, for_tail=False)
hitfx.append(HitFX(x, y, t, color, lr, variant))
```
Note: `scroll_target = sc_now` (not `scroll_hit`), so dy = 0 → position is exactly
on the judgment line at the note's horizontal lane.

### C++ (current)
`hold_tick_fx` is not implemented. No periodic HitFX during hold.

### Alignment Target
- Track `next_hold_fx_ms` in C++ `NoteState`.
- Each engine step, for every actively-held hold note, when `now_ms >= next_hold_fx_ms`:
  - Evaluate `LineAnim` at `now_sec_` for `lx, ly, rot, sc_now`.
  - Compute world position with `scroll_target = sc_now` (dy = 0 → on line).
  - Emit a `JudgeEvent` of a new type `HoldTick`, or emit a dedicated `HitFxEvent`.
  - Increment `next_hold_fx_ms += hold_fx_interval_ms`.

---

## 7. Metric 5 — Note Visibility / `t_enter` Precomputation

### Python (`engine/visibility.py` lines 88-175)
```python
# For each note, scan backward from t_hit:
# 1. Exponential search: find first invisible time
# 2. Binary search (20 iters): refine boundary
# → sets note.t_enter = first time note is on-screen
# Then in renderer: if t_draw < note.t_enter: skip
```
Visibility check: `_note_visible_on_screen()` evaluates full world position + AABB vs screen.

Culling rules (Python, `frame_renderer.py` lines 372-380):
```python
if t_draw < note.t_enter:                       continue  # pre-entry cull
t_end_for_cull = t_end if hold else t_hit
extra_after = approach + 0.5  # tap/drag/flick
extra_after = 0.35             # hold
if t_draw > t_end_for_cull + extra_after:       continue  # post-exit cull
```

### C++ (`engine.cpp` lines 282-292)
```cpp
const double min_t_hit = now_sec_ - (kBadWindowSec / flow);
const double max_t_hit = now_sec_ + ((approach * overrender) / flow);
// Only time-range culling, no t_enter precomputation
```

### Gap
- No `t_enter` field in `RuntimeNote`.
- No precomputation pass at chart load time.
- No post-exit cull with type-dependent extra window.

### Alignment Target
- Add `t_enter: double` to `RuntimeNote` (default `-1e9` = always visible).
- After chart load + mods, run `precompute_t_enter()` equivalent:
  - For each note, binary-search for first on-screen time using `LineAnim` eval.
- In `build_frame_commands()`, add:
  ```cpp
  if (!cfg_.no_cull_enter_time && now_sec_ < note.t_enter) continue;
  const double t_end_for_cull = (note.kind == Hold) ? note.hold_end : note.t_hit;
  const double extra = (note.kind == Hold) ? 0.35 : (approach + 0.5);
  if (now_sec_ > t_end_for_cull + extra) continue;
  ```


---

## 8. Metric 6 — Miss Note Visual Dimming

### Python (`frame_renderer.py` lines 424-443)
```python
MISS_FADE_SEC = 0.35

miss_dim = 0.0
if s.miss:
    dtm = t_draw - s.miss_t   # s.miss_t = time note was marked missed
    if dtm >= 0.0:
        miss_dim = clamp(dtm / MISS_FADE_SEC, 0.0, 1.0)
        if note.kind == 3:  # hold
            # hold body dims but doesn't disappear immediately
            base_dim = max((1.0 - miss_dim) * 0.65, 0.18)
            note_alpha *= base_dim
            # after t_end: fade to 0
        else:
            note_alpha *= (1.0 - miss_dim) * 0.65
```
Missed notes remain visible for up to `MISS_FADE_SEC` and fade out, greyed to ~65%.

### C++ (current)
- `NoteState` has `bool miss` but no `miss_t` (timestamp of miss event).
- After `judged=true`, note is immediately removed from `build_frame_commands()` output.
- No fade, no greying.

### Alignment Target
- Add `double miss_t = -1e9` to C++ `NoteState`.
- Set `miss_t = now_sec_` when marking miss.
- In `build_frame_commands()`: do NOT cull judged+miss notes until `now_sec_ > miss_t + 0.35`.
- Pass `miss_dim` in `FrameCommand` so SwRenderer can apply grey tint + alpha reduction.

---

## 9. Metric 7 — Line Alpha Affects Notes

### Python (`frame_renderer.py` lines 416-422)
```python
# note_alpha starts at note.alpha01
la01 = line.alpha  # evaluated 0..1
la_raw = raw line alpha value

if la_raw < 0.0:
    if line_alpha_affects_notes != "never":
        note_alpha *= clamp(1.0 + la_raw, 0.0, 1.0)  # negative alpha dims note
elif line_alpha_affects_notes == "always":
    note_alpha *= clamp(la01, 0.0, 1.0)
# default mode "negative_only": only negative alpha values propagate to notes
```

Three modes (configured per-render, default `"negative_only"`):
| Mode | Effect |
|------|--------|
| `"never"` | Line alpha never affects notes |
| `"negative_only"` | Only negative line alpha dims notes (default) |
| `"always"` | Line alpha always multiplies note alpha |

### C++ (current)
Not implemented. Note alpha is taken directly from `note.alpha01`.

### Alignment Target
- Add `line_alpha_affects_notes` mode (enum) to `RenderConfig` (already exists as `std::string`).
- In `build_frame_commands()`, after evaluating `LineAnim.eval_alpha(t)`:
  ```cpp
  const double la_raw = line.anim.alpha_raw.eval(now_sec_);
  if (la_raw < 0.0 && cfg_.line_alpha_affects_notes != "never")
      cmd.alpha *= std::clamp(1.0 + la_raw, 0.0, 1.0);
  else if (cfg_.line_alpha_affects_notes == "always")
      cmd.alpha *= std::clamp(std::abs(la_raw), 0.0, 1.0);
  ```


---

## 10. Required C++ Type Changes

### 10.1 `RuntimeNote` additions (`types.hpp`)
```cpp
struct RuntimeNote {
    // --- existing ---
    int id        = 0;
    int line_id   = 0;
    int lane      = 0;       // keep for legacy; used as fallback only
    bool above    = true;    // NEW (was missing)
    bool fake     = false;
    double t_hit  = 0.0;
    double hold_end = 0.0;
    double speed_mul = 1.0;
    double alpha01   = 1.0;
    NoteKind kind    = NoteKind::Tap;

    // --- NEW fields ---
    double x_local_px  = 0.0;   // offset along line tangent (px at W=1280)
    double y_offset_px = 0.0;   // additional normal offset (px, rarely non-zero)
    double scroll_hit  = 0.0;   // IntegralTrack.integral(t_hit)  [px]
    double scroll_end  = 0.0;   // IntegralTrack.integral(hold_end) [px]
    double t_enter     = -1e9;  // first on-screen time (precomputed)
    bool   mh          = false; // multi-hit: simultaneous with another note
    uint8_t tint_r = 255, tint_g = 255, tint_b = 255;         // note body tint
    uint8_t tint_fx_r = 255, tint_fx_g = 255, tint_fx_b = 255;// hitfx tint
    bool    has_tint_fx = false; // whether tint_fx overrides judge color
};
```

### 10.2 `FrameCommand` additions (`types.hpp`)
```cpp
struct FrameCommand {
    // --- existing ---
    Type     type       = Type::DrawNote;
    int      note_id    = 0;
    int      lane       = 0;
    NoteKind kind       = NoteKind::Tap;
    float    x          = 0.0f;   // CHANGE: world px, not normalised
    float    y          = 0.0f;   // CHANGE: world px, not normalised
    float    alpha      = 1.0f;
    double   t_hit_sec  = 0.0;
    double   hold_end_sec = 0.0;

    // --- NEW fields ---
    float  head_x       = 0.0f;  // hold head world x (px)
    float  head_y       = 0.0f;  // hold head world y (px)
    float  tail_x       = 0.0f;  // hold tail world x (px)
    float  tail_y       = 0.0f;  // hold tail world y (px)
    float  hold_progress = 0.0f; // 0..1, how much of hold is consumed
    float  rot          = 0.0f;  // line rotation (radians) at this frame
    bool   above        = true;
    bool   mh           = false;
    bool   miss         = false;
    float  miss_dim     = 0.0f;  // 0..1, fade factor for missed notes
    uint8_t tint_r = 255, tint_g = 255, tint_b = 255;
};
```

### 10.3 `NoteState` additions (`engine.hpp` private)
```cpp
struct NoteState {
    bool   judged         = false;
    bool   hit            = false;
    bool   miss           = false;
    bool   holding        = false;     // NEW
    bool   hold_finalized = false;     // NEW
    bool   hold_failed    = false;     // NEW
    double miss_t         = -1e9;      // NEW: timestamp of miss judgment
    int    next_hold_fx_ms = 0;        // NEW: for periodic hold hitfx
    int    hold_grade     = 0;         // NEW: 1=perfect,2=good,3=bad
};
```

### 10.4 New `JudgeFxEvent` in `StepResult`
```cpp
struct HitFxEvent {
    double world_x, world_y;  // evaluated at judgment time
    double rot;               // line rotation at judgment time
    double time_sec;          // judgment time
    int    grade;             // 1=perfect,2=good,3=bad
    bool   is_hold_tick;      // true = periodic hold fx
    uint8_t r, g, b;          // resolved color
};
// Add to StepResult:
std::vector<HitFxEvent> hit_fx_events;
```


---

## 11. Test Oracle Strategy

Extend `phic_port/tests/oracle_compare.py` with a **render-position oracle** that:
1. Advances a chart to a fixed time `t_check`.
2. Collects Python `(world_x, world_y)` for each visible note via `note_world_pos()`.
3. Collects C++ `(world_x, world_y)` from `FrameCommand` output of `engine.step()`.
4. Asserts positions match within tolerance (suggested `2.0 px` at 1280×720).

### New oracle case outline
```python
def canonical_render_positions_python(fmt, payload, t_check, W=1280, H=720):
    _, lines, notes = load_chart(fmt, payload, W, H)
    # precompute scroll
    for n in notes:
        ln = lines[n.line_id]
        n.scroll_hit = ln.scroll_px.integral(n.t_hit)
    results = []
    for n in notes:
        if n.t_hit - 3.0 <= t_check <= n.t_hit + 0.5:
            ln = lines[n.line_id]
            lx, ly, rot, _, sc, _ = eval_line_state(ln, t_check)
            x, y = note_world_pos(lx, ly, rot, sc, n, n.scroll_hit)
            results.append((n.nid, round(x, 2), round(y, 2)))
    return sorted(results)

def canonical_render_positions_cpp(exe, chart_path, fmt, t_check):
    out = subprocess.check_output(
        [str(exe), "--input", str(chart_path), "--format", fmt,
         "--step-to", str(t_check), "--emit-frame-commands"],
        text=True)
    obj = json.loads(out)
    results = []
    for cmd in obj.get("frame_commands", []):
        results.append((cmd["note_id"],
                        round(cmd["world_x"], 2),
                        round(cmd["world_y"], 2)))
    return sorted(results)
```

### Tolerance guidelines
| Position type | Tolerance |
|---------------|-----------|
| Note world x/y | 2.0 px |
| Hold head x/y | 2.0 px |
| Hold tail x/y | 2.0 px |
| HitFX x/y | 3.0 px (t_fx interpolation) |
| Hold progress | 0.01 (1%) |

---

## 12. Summary Table

| # | Metric | Python source | C++ status | Priority |
|---|--------|--------------|------------|----------|
| 1 | Note world position | `kinematics.note_world_pos` | ❌ lane-grid only | **P0** |
| 2 | Hold head/tail/progress | `frame_renderer` lines 450-532 | ❌ not rendered | **P0** |
| 3 | HitFX spawn position | `pygame_backend` lines 2022-2034 | ❌ wrong coords | **P0** |
| 4 | Periodic hold HitFX | `hold/logic.hold_tick_fx` | ❌ not implemented | **P1** |
| 5 | `t_enter` precomputation | `visibility.precompute_t_enter` | ❌ not implemented | **P1** |
| 6 | Miss note fade | `frame_renderer` lines 424-443 | ❌ immediate cull | **P1** |
| 7 | `line_alpha_affects_notes` | `frame_renderer` lines 416-422 | ❌ not applied | **P2** |
| 8 | Multi-hit flag `mh` | `chart_init.group_simultaneous_notes` | ❌ missing field | **P2** |
| 9 | Note tint (`tint_rgb`) | `RuntimeNote.tint_rgb` | ❌ missing field | **P2** |

### P0 = breaks positioning of every note and every HitFX
### P1 = visible quality regression
### P2 = minor visual fidelity gap

---

## 13. Implementation Order Recommendation

```
1. Extend RuntimeNote + parser (scroll_hit, scroll_end, x_local_px, above, t_enter, mh, tint)
2. Fix build_frame_commands(): world-position formula using LineAnim
3. Fix FrameCommand: world_x/y, rot, above, head_x/y, tail_x/y, hold_progress
4. Fix SwRenderer: draw notes at world coords, draw hold body head→tail
5. Fix HitFX: emit HitFxEvent from engine at judgment time with world pos
6. Add hold tick FX in engine step loop
7. Add miss_t + miss fade in FrameCommand + SwRenderer
8. Add line_alpha_affects_notes in build_frame_commands
9. Add render-position oracle tests
```

