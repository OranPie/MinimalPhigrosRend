# Python Reference: Note Lifecycle from Parse to Render Complete

## Overview
This document traces the complete lifecycle of a note object from raw chart data through rendering, covering all Python reference implementations in MinimalPhigrosRend.

---

## Phase 1: CHART PARSING → RuntimeNote Creation

### Input
- Raw chart file (Official .json, RPE .json, PEC text, or compiled .pcc)

### Process Location
**`phic_renderer/assets/loader.py`**: `load_chart(path, W, H)` → dispatches to format-specific loaders

**Format-Specific Parsers:**
- `phic_renderer/chart/official.py`: `load_official(data, W, H)`
- `phic_renderer/chart/rpe.py`: `load_rpe(data, W, H)`
- `phic_renderer/chart/pec.py`: `load_pec(path, W, H)`

### Output: RuntimeNote Structure
**Defined in:** `phic_renderer/types.py` → `RuntimeNote` dataclass

```python
@dataclass
class RuntimeNote:
    nid: int                          # Note ID (unique within chart)
    line_id: int                      # Judgment line this note belongs to
    kind: int                         # 1=tap, 2=drag, 3=hold, 4=flick
    above: bool                       # Above (True) or below (False) judgment line
    fake: bool                        # Fake note (invisible, not judged)
    t_hit: float                      # Hit time in seconds
    t_end: float                      # End time (for holds: t_hit + hold_duration)
    x_local_px: float                 # Horizontal offset from line center (pixels)
    y_offset_px: float                # Vertical offset (unused in most cases)
    speed_mul: float                  # Speed multiplier (Official: hold tail speed; RPE: general)
    size_px: float                    # Size scale factor
    alpha01: float                    # Opacity (0-1)
    tint_rgb: Tuple[int,int,int]     # Color tint (default white)
    tint_hitfx_rgb: Optional[Tuple]   # Hit effect color override (RPE specific)
```

**Example from official.py (line 186-200):**
```python
note = RuntimeNote(
    nid=nid,
    line_id=i,
    kind=kind,                              # From n["type"]
    above=above,
    fake=False,
    t_hit=u_to_sec(float(n["time"]), bpm), # BPM-adjusted time
    t_end=t_hit + u_to_sec(hold_u, bpm) if kind == 3 and hold_u > 0 else t_hit,
    x_local_px=float(n.get("positionX", 0.0)) * Uw,
    y_offset_px=0.0,
    speed_mul=float(n.get("speed", 1.0)),
    size_px=1.0,
    alpha01=1.0,
)
```

---

## Phase 2: SCROLL CACHING → scroll_hit & scroll_end

### Why Needed
The scroll value must be precomputed because it's used for positioning notes relative to their judgment line's scroll track. The scroll position changes over time as the line moves, and notes need to know their "target" scroll positions at hit and end times.

### Location
Done in two places:

**A) In chart parsers (official.py lines 210-224):**
```python
# After all notes created
line_map = {ln.lid: ln for ln in lines_out}
for n in notes_out:
    ln = line_map[n.line_id]
    n.scroll_hit = ln.scroll_px.integral(n.t_hit)      # Scroll at hit time
    if int(n.kind) == 3 and float(n.t_end) > float(n.t_hit):
        # For holds, calculate tail scroll position
        dur = max(0.0, float(n.t_end) - float(n.t_hit))
        sp = max(0.0, float(n.speed_mul))
        n.scroll_end = float(n.scroll_hit) + sp * dur * float(Uh)
        n.speed_mul = 1.0  # Reset since applied to scroll_end
    else:
        n.scroll_end = ln.scroll_px.integral(n.t_end)
```

**B) In pygame_backend.py (lines 799-800) - may be recomputed:**
```python
n.scroll_hit = ln.scroll_px.integral(n.t_hit)
n.scroll_end = ln.scroll_px.integral(n.t_end)
```

### Scroll Tracks
`RuntimeLine.scroll_px` is an `IntegralTrack` object that computes cumulative scroll distance over time:
- **IntegralTrack**: Piecewise linear function that integrates velocity to get position
- **Seg1D**: Represents velocity from t0 to t1, with acceleration/deceleration

---

## Phase 3: MOD APPLICATION → Transforming Notes

### Location
`phic_renderer/engine/mods/` directory. Applied in pygame_backend.py line 804:
```python
seg_notes = apply_mods(dict(mods_cfg_local), seg_notes, seg_lines)
```

### Available Mods
- **attach.py**: Attach notes to held notes
- **full_blue.py**: Convert all notes to blue
- **compress_zip.py**: Compress/quantize note timings
- **fade.py**: Apply alpha fade to notes
- **stutter.py**: Repeat notes multiple times
- **thin_out.py**: Remove notes based on rules
- **rules.py**: Filter notes by type/line/properties

---

## Phase 4: SIMULTANEOUS GROUPING → mh (multi-hit) Flag

### Location
`phic_renderer/engine/chart_init.py`: `group_simultaneous_notes(notes, eps=1e-4)`

### Purpose
Mark notes that hit at the same time (within tolerance) with the `mh` flag, used for rendering/audio feedback.

```python
def group_simultaneous_notes(notes: List[RuntimeNote], eps: float = 1e-4):
    i = 0
    while i < len(notes):
        j = i + 1
        while j < len(notes) and abs(notes[j].t_hit - notes[i].t_hit) <= eps:
            j += 1
        if (j - i) >= 2:  # 2+ notes at same time
            for k in range(i, j):
                notes[k].mh = True
        i = j
```

---

## Phase 5: VISIBILITY PRECOMPUTATION → t_enter

### Location
`phic_renderer/engine/visibility.py`: `precompute_t_enter(lines, notes, W, H)`

### Purpose
Calculate when each note **first becomes visible on screen**. This is critical for:
- Visibility culling (don't render notes before they appear)
- Animation timing
- Avoiding "pop-in" artifacts

### Algorithm
1. **Backward scan from t_hit**: Starting from note's hit time, scan backward to find a time when note is visible
2. **Exponential search**: Roughly locate the invisible→visible boundary
3. **Binary search**: Refine the boundary to find exact `t_enter` time

**Key function:** `_note_visible_on_screen(lines, note, t, W, H, ...)` checks if note position at time t is within screen bounds.

### Result
Each `RuntimeNote.t_enter` is set to the first time the note becomes visible.

---

## Phase 6: NOTE STATE WRAPPING → NoteState

### Location
`phic_renderer/renderer/pygame_backend.py` line 1229:
```python
states = [NoteState(n) for n in notes]
```

### NoteState Structure
**Defined in:** `phic_renderer/types.py` → `NoteState` dataclass

```python
@dataclass
class NoteState:
    note: RuntimeNote                 # Reference to the RuntimeNote
    judged: bool = False              # Has judgment been finalized?
    hit: bool = False                 # Was note hit (not missed)?
    holding: bool = False             # Is hold note currently being held?
    released_early: bool = False      # Was hold released before completion?
    miss: bool = False                # Was note judged as miss?
    next_hold_fx_ms: int = 0          # Next hold effect time (milliseconds)
    hold_grade: Optional[str] = None  # Grade for hold note (PERFECT/GOOD/BAD)
    hold_finalized: bool = False      # Has hold been finalized?
    hold_failed: bool = False         # Did hold fail?
```

**Purpose:** Track runtime state of each note as the game progresses.

---

## Phase 7: PER-FRAME RENDERING LOOP

### Location
`phic_renderer/renderer/pygame_backend.py` main loop (lines 1800+)

### Loop Structure
```
while game_running:
    t = current_time()
    
    # === Update Phase ===
    # 1. Autoplay or manual judgment
    if autoplay:
        # Loop through notes, apply judgment from plan
        # Spawn HitFX, update NoteState
    else:
        # Check pointers against notes for manual judgment
    
    # 2. Hold system updates
    hold_system.maintenance(...)       # Check if fingers still holding
    hold_system.finalize(...)          # Finalize completed holds
    hold_system.tick_effects(...)      # Spawn hold effects
    
    # 3. Miss detection
    detect_misses(...)                 # Catch notes past miss window
    
    # === Render Phase ===
    base_surface = render_frame_impl(t, ...)
    screen.blit(base_surface)
```

---

## Phase 8: RENDERING - Note Position Calculation

### Location
`phic_renderer/backends/pygame/rendering/frame_renderer.py` lines 345-652

### Two-Pass Rendering
**Pass 1:** Hold notes (kind==3)
**Pass 2:** Tap/Drag/Flick notes (kind!=3)

### Position Calculation
For each note at render time `t_draw`:

**Step 1: Evaluate line state**
```python
lx, ly, lr, la01, sc_now, la_raw = line_states[n.line_id]
# lx, ly: line position in world coordinates
# lr: line rotation (radians)
# la01: line alpha (0-1)
# sc_now: current scroll position (pixels)
```

**Step 2: Calculate dy (distance from line)**
```python
dy = (n.scroll_hit - sc_now) * flow_multiplier   # For tap/drag
dy = (n.scroll_end - sc_now) * flow_multiplier * speed_mul  # For hold tail
```

**Step 3: Calculate world position**
```python
# Tangent & normal vectors along line
tx, ty = cos(lr), sin(lr)          # Along line
nx, ny = -sin(lr), cos(lr)         # Perpendicular

# Local offsets
x_local = n.x_local_px             # Horizontal offset
sgn = 1.0 if n.above else -1.0
y_local = sgn * dy + n.y_offset_px  # Vertical offset

# World position
x = lx + tx * x_local + nx * y_local
y = ly + ty * x_local + ny * y_local
```

**Step 4: Screen projection with expand/overrender**
```python
x_screen, y_screen = apply_expand_xy(
    x * overrender, y * overrender,
    RW, RH, expand
)
```

### Culling Checks
```python
# 1. Time-based: note hasn't entered screen yet?
if t_draw < n.t_enter:
    continue

# 2. Screen-based: note off-screen with margin?
if (x_screen < -margin or x_screen > RW + margin or
    y_screen < -margin or y_screen > RH + margin):
    continue

# 3. Hold-specific: past t_end?
if n.kind == 3 and t_draw >= n.t_end:
    continue
```

---

## Phase 9: RENDERING - Hold Note Drawing

### Location
`phic_renderer/backends/pygame/rendering/frame_renderer.py` lines 450-532

### Hold Drawing Process
```python
# Calculate head position (where hold starts)
head_target_scroll = n.scroll_hit if sc_now <= n.scroll_hit else sc_now
dy_head = (head_target_scroll - sc_now) * flow_mul
head = (lx + tx * x_local + nx * sgn * dy_head,
        ly + ty * x_local + ny * sgn * dy_head)

# Calculate tail position (where hold ends)
dy_tail = (n.scroll_end - sc_now) * flow_mul * speed_mul
tail = (lx + tx * x_local + nx * sgn * dy_tail,
        ly + ty * x_local + ny * sgn * dy_tail)

# Calculate progress (0-1)
if sc_hit != sc_end:
    prog = clamp((sc_now - sc_hit) / (sc_end - sc_hit), 0, 1)
else:
    prog = clamp((t_draw - t_hit) / (t_end - t_hit), 0, 1)

# Draw using 3-slice technique
draw_hold_3slice(overlay, head, tail, lr, hold_alpha, ...)
```

**3-slice technique:** Renders head cap, body (scaled), tail cap for clean appearance at any length.

---

## Phase 10: RENDERING - Tap/Drag/Flick Drawing

### Location
`phic_renderer/backends/pygame/rendering/frame_renderer.py` lines 563-651

### Process
```python
# Calculate note position
dy = (n.scroll_hit - sc_now) * flow_mul * speed_mul
p = (lx + tx * x_local + nx * sgn * dy,
     ly + ty * x_local + ny * sgn * dy)
ps = apply_expand_xy(p[0] * overrender, p[1] * overrender, RW, RH, expand)

# Get note image from resource pack
img = pick_note_image(n, respack)

# Scale image to screen size
target_w = base_note_w * note_scale_x * size_px
scaled = pygame.transform.smoothscale(img, (target_w, target_h))

# Rotate to match line rotation
rotated = pygame.transform.rotate(scaled, -lr * 180 / π)

# Apply tint and alpha
rotated.fill((r, g, b, 255), special_flags=pygame.BLEND_RGBA_MULT)
rotated.set_alpha(int(255 * note_alpha))

# Blit to screen
overlay.blit(rotated, (ps[0] - w/2, ps[1] - h/2))

# Draw outline if enabled
if draw_outline:
    draw_poly_outline_rgba(overlay, pts, rgba_outline, width=outline_w)
```

---

## Phase 11: HIT EFFECTS (HITFX) SPAWNING

### Spawn Locations

**A) On note judgment (autoplay or manual)**
`phic_renderer/renderer/pygame_backend.py` lines 2024-2034:
```python
x, y = note_world_pos(lx, ly, lr, sc, n, n.scroll_hit, for_tail=False)
c = (255, 255, 255, 255)
if getattr(n, "tint_hitfx_rgb", None) is not None:
    rr, gg, bb = n.tint_hitfx_rgb
    c = (int(rr), int(gg), int(bb), 255)
elif respack:
    c = respack.judge_colors.get("PERFECT", c)

var = "good" if grade == "GOOD" else ""
hitfx.append(HitFX(x, y, t_fx, c, lr, var))

if respack and not respack.hide_particles:
    particles.append(ParticleBurst(x, y, int(t_fx * 1000), int(hitfx_duration * 1000), c))
```

**B) On hold note held (periodic)**
`phic_renderer/backends/pygame/hold/logic.py` lines 237-256:
```python
while now_tick >= s.next_hold_fx_ms and t < n.t_end:
    ln = lines[n.line_id]
    lx, ly, lr, _, sc_now, _ = eval_line_state(ln, t)
    x, y = note_world_pos(lx, ly, lr, sc_now, n, sc_now, for_tail=False)
    g = str(getattr(s, "hold_grade", None) or "PERFECT").upper()
    c = respack.judge_colors.get(g, ...)
    
    hitfx.append(HitFX_cls(x, y, float(t), c, lr, var))
    if not respack.hide_particles:
        particles.append(ParticleBurst_cls(x, y, ...))
    
    s.next_hold_fx_ms += hold_fx_interval_ms
```

### HitFX Structure
**Defined in:** `phic_renderer/engine/effects.py`

```python
@dataclass
class HitFX:
    x: float                           # World X position
    y: float                           # World Y position
    t0: float                          # Spawn time
    rgba: Tuple[int,int,int,int]      # Color + alpha
    rot: float                         # Line rotation (radians)
    variant: str = ""                 # "good" or "" (PERFECT)
```

---

## Phase 12: HIT EFFECT RENDERING

### Location
`phic_renderer/backends/pygame/effects/hitfx.py`: `draw_hitfx(overlay, fx, t, ...)`

### Process
```python
age = t - fx.t0                        # How long since spawn
if age < 0 or age > duration:
    return                            # Too old, skip

# Find frame in animation sheet
p = clamp(age / duration, 0, 0.999999)
idx = int(p * (fw * fh))             # Frame index
ix = idx % fw                        # Column
iy = idx // fw                       # Row

# Extract frame from sheet
frame = sheet.subsurface((ix * cell_w, iy * cell_h, cell_w, cell_h))

# Scale and rotate
sc = (hitfx_scale * hitfx_scale_mul) / expand
if sc != 1.0:
    frame = pygame.transform.smoothscale(frame, (cell_w * sc, cell_h * sc))

if respack.hitfx_rotate:
    frame = pygame.transform.rotozoom(frame, -fx.rot * 180 / π, 1.0)

# Apply tint
if respack.hitfx_tinted or (r,g,b) != (255,255,255):
    tint_s = pygame.Surface(frame.get_size(), pygame.SRCALPHA)
    tint_s.fill((r, g, b, 255))
    frame = frame.copy()
    frame.blit(tint_s, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)

frame.set_alpha(a)

# Blit at expanded position
x0, y0 = apply_expand_xy(fx.x * overrender, fx.y * overrender, W, H, expand)
overlay.blit(frame, (x0 - w/2, y0 - h/2))
```

### Timeline
- **0 to ~0.18s (or respack duration)**: HitFX visible, animating
- **Beyond duration**: Pruned from list, not rendered

---

## Phase 13: PARTICLE RENDERING

### ParticleBurst Structure
`phic_renderer/engine/effects.py` lines 18-51

```python
class ParticleBurst:
    def __init__(self, x, y, start_ms, duration_ms, rgba, count=4):
        self.x, self.y = x, y
        self.start = start_ms
        self.duration = duration_ms
        self.rgba = rgba
        # Generate random particles with speed & angle
        self.pa = [(random(185, 265), random(0, 2π)) for _ in range(count)]
    
    def get_particles(self, now_ms):
        tick = (now_ms - start) / duration  # 0..1
        alpha = int(255 * (1 - tick))
        size = 20 * cubic_ease(tick)
        
        particles = []
        for spd, ang in self.pa:
            dist = spd * (9*tick / (8*tick + 1)) / 2  # Easing function
            px = self.x + dist * cos(ang)
            py = self.y + dist * sin(ang)
            particles.append({'x': int(px), 'y': int(py), 'size': size, 'color': (r,g,b,alpha)})
        return particles
```

### Rendering
`phic_renderer/backends/pygame/effects/particles.py`: `draw_particles(overlay, particles, ...)`

Each particle is drawn as a circle with size and alpha based on time.

---

## Phase 14: RENDER COMPLETE

### Frame Composition
```
base_surface (background + lines)
    ↓
overlay.blit(base_surface)             # BG
    ↓
[render holds]
[render tap/drag/flick]
[render debug info if enabled]
    ↓
overlay.blit(hitfx for each fx)        # HitFX
overlay.draw_particles(...)            # Particles
    ↓
screen.blit(base_surface)              # To display
```

### Pruning
Before rendering, old effects are removed:
```python
hitfx[:] = prune_hitfx(hitfx, t, duration)
particles[:] = prune_particles(particles, now_ms)
```

---

## Summary: Key Transformations

| Phase | Data In | Data Out | Key Fields Changed |
|-------|---------|----------|-------------------|
| 1. Parse | Raw chart JSON/text | `RuntimeNote` | All fields initialized |
| 2. Scroll Cache | `RuntimeNote`, `RuntimeLine.scroll_px` | `RuntimeNote` | `scroll_hit`, `scroll_end` |
| 3. Mods | `RuntimeNote` list | Modified `RuntimeNote` list | `kind`, `t_hit`, `alpha01`, etc. |
| 4. Grouping | `RuntimeNote` list | Same, with flag | `mh` = True for simultaneous |
| 5. Visibility | `RuntimeNote` list, lines | Same | `t_enter` precomputed |
| 6. State | `RuntimeNote` | `NoteState(note)` | Runtime state fields initialized |
| 7-12. Rendering | `NoteState`, time `t` | Screen coordinates | `judged`, `hit`, `holding`, `miss` updated |
| 13-14. Effects | Judgment event | `HitFX`, `ParticleBurst` | Visible on screen |

---

## Critical C++ Implementation Notes

The C++ port (`phic_port/`) must replicate:

1. **Scroll integral calculation** - Must match `IntegralTrack.integral(t)` exactly
2. **Position calculation** - Tangent/normal vectors, `note_world_pos()` formula
3. **Visibility precomputation** - `t_enter` algorithm with exponential→binary search
4. **Hold tail scroll** - `scroll_end = scroll_hit + speed_mul * duration * Uh` (Official) vs per-segment (RPE)
5. **HitFX timing** - Must spawn at exact judgment time with correct world position
6. **Hold effects** - Periodic effects at `next_hold_fx_ms` intervals
7. **Particle physics** - Distance formula: `spd * (9*tick / (8*tick+1)) / 2`

