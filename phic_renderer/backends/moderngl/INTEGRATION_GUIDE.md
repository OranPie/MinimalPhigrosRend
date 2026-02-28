# ModernGL Backend Integration Guide

Complete guide for integrating the new modular architecture into `app.py`.

## Overview

All modules are implemented and ready for integration. This guide shows how to wire them together to replace the monolithic `app.py` implementation.

## Module Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         app.py                               │
│                  (Main Coordinator)                          │
└───────────────────────┬─────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┬───────────────┐
        │               │               │               │
┌───────▼──────┐  ┌────▼────┐  ┌──────▼──────┐  ┌────▼─────┐
│ FrameRenderer│  │ Effects │  │  Resources  │  │   Hold   │
│              │  │ System  │  │  Managers   │  │ Renderer │
└──────┬───────┘  └─────┬───┘  └──────┬──────┘  └──────────┘
       │                │              │
  ┌────┼────┐      ┌────┼────┐    ┌───┼───┐
  │    │    │      │    │    │    │   │   │
Lines Notes GPU   Part Hit  Font Tex  BG
               Trail  FX  FX Cache Mgr  Mgr
```

## Step-by-Step Integration

### Phase 1: Import New Modules

Add imports to `app.py`:

```python
# Existing imports
from .renderer2d import create_renderer2d
from .sprite import create_sprite_program
# ...

# NEW: Resource management
from .resources.font_cache import FontCache
from .resources.background import BackgroundManager
from .resources.texture_manager import GLTextureManager, create_texture_manager_from_respack

# NEW: Effects
from .effects.particles import ParticleSystem
from .effects.hitfx import HitEffectManager
from .effects.motion_blur import MotionBlurEffect, get_motion_blur_config
from .effects.trail import GPUTrailEffect

# NEW: Rendering
from .hold.renderer import HoldRenderer
from .rendering.notes import NoteRenderer
from .rendering.lines import LineRenderer
from .rendering.frame_renderer import FrameRenderer
from .rendering.batch import InstancedBatchRenderer
```

### Phase 2: Initialize Modules in `GLApp.__init__()`

Replace inline initialization with module instantiation:

```python
@dataclass
class GLApp:
    ctx: Any
    window_size: tuple[int, int]
    r2d: Any
    sprite: Any
    args: Any
    render_ctx: Dict[str, Any]
    t0: float

    # NEW: Replace inline caches with module instances
    _font_cache: FontCache = field(default=None)
    _bg_manager: BackgroundManager = field(default=None)
    _texture_manager: Optional[GLTextureManager] = field(default=None)
    _batch_renderer: Optional[InstancedBatchRenderer] = field(default=None)
    _particle_system: ParticleSystem = field(default=None)
    _hitfx_manager: HitEffectManager = field(default=None)
    _motion_blur: MotionBlurEffect = field(default=None)
    _trail_effect: Optional[GPUTrailEffect] = field(default=None)
    _hold_renderer: HoldRenderer = field(default=None)
    _note_renderer: NoteRenderer = field(default=None)
    _line_renderer: LineRenderer = field(default=None)
    _frame_renderer: FrameRenderer = field(default=None)

    # Keep existing fields
    _states: List[NoteState] = field(default_factory=list)
    _judge: Any = None
    _down: bool = False
    _press_edge: bool = False
    # ...

    def __post_init__(self):
        """Initialize all subsystems."""
        # Font cache
        self._font_cache = FontCache(self.ctx, max_cache_size=128)

        # Background manager
        self._bg_manager = BackgroundManager(self.ctx)

        # Effects
        self._particle_system = ParticleSystem(self.ctx)
        self._hitfx_manager = HitEffectManager(self.ctx)
        self._motion_blur = MotionBlurEffect(self.ctx)

        # Hold renderer
        self._hold_renderer = HoldRenderer(self.ctx, self.sprite, self.window_size)

        # Note and line renderers
        self._line_renderer = LineRenderer(self.ctx, self.window_size, self._font_cache)
        self._note_renderer = NoteRenderer(self.ctx, self.window_size, self._hold_renderer)

        # Frame renderer coordinator
        self._frame_renderer = FrameRenderer(
            self.ctx, self.sprite, self.r2d, self.window_size,
            self._line_renderer, self._note_renderer,
            self._particle_system, self._hitfx_manager
        )

        # Optional: Initialize texture manager if respack available
        # (Defer to first use, or initialize here if respack known)

        # Optional: Initialize trail effect if enabled
        # W, H = self.window_size
        # self._trail_effect = GPUTrailEffect(self.ctx, W, H, max_frames=8)
```

### Phase 3: Replace `_get_text_texture()` Calls

Find all calls to `self._get_text_texture()` and replace with font cache:

```python
# OLD:
tex = self._get_text_texture(text, font_path=..., font_size=...)

# NEW:
tex = self._font_cache.get_text_texture(text, font_path=..., font_size=...)
```

### Phase 4: Replace `_render_scene()` Implementation

The core rendering loop can now use the frame renderer:

```python
def _render_scene(self, t: float) -> None:
    """Render the game scene (delegated to FrameRenderer)."""

    # Time adjustment logic (keep existing)
    offset = float(self.render_ctx.get("offset", 0.0) or 0.0)
    # ... (audio sync logic) ...

    # Use frame renderer
    self._ensure_judge_state()
    note_render_count = self._frame_renderer.render_frame(
        t, self.render_ctx, self._states, self.args
    )
    self._note_render_count_last = note_render_count
```

### Phase 5: Replace Hit Effect Updates

Replace `_update_hitfx()` with hitfx manager:

```python
def _update_hitfx(self, t: float, dt: float) -> None:
    """Update hit effects (delegated to HitEffectManager)."""
    respack = self.render_ctx.get("respack", None)

    # Update particles
    now_tick = self._now_tick_ms(float(dt))
    self._last_tick_ms = int(now_tick)
    self._particle_system.update(now_tick)

    # Update hit effects
    self._hitfx_manager.update(t, respack)

    # Autoplay logic (keep existing)
    if bool(getattr(self.args, "autoplay", False)):
        # ... (existing autoplay code) ...
        # When adding hit effects:
        self._hitfx_manager.add_effect(x, y, t, color, lr, variant)
        self._particle_system.add_burst(x, y, now_tick, duration, color)
```

### Phase 6: Replace Particle Rendering

Replace `_draw_particles()` with particle system:

```python
# OLD:
def _draw_particles(self) -> None:
    # ... 50 lines of code ...

# NEW:
def _draw_particles(self) -> None:
    respack = self.render_ctx.get("respack", None)
    expand = float(self.render_ctx.get("expand") or 1.0)
    now_tick = int(getattr(self, "_last_tick_ms", 0) or 0)
    self._frame_renderer.render_particles(now_tick, expand, respack)
```

### Phase 7: Integrate Motion Blur

Update `render()` to use motion blur module:

```python
def render(self, dt: float) -> None:
    state = self.render_ctx.get("state", None)
    chart_speed = float(getattr(self.args, "chart_speed", 1.0) or 1.0)
    t_base = # ... calculate base time ...

    # Get motion blur config
    samples, shutter = get_motion_blur_config(state)

    # Clear screen
    self.ctx.clear(0.06, 0.06, 0.08, 1.0)

    # Render with motion blur
    def render_scene_at_time(t_sample: float, weight: float):
        self._set_weight(weight)
        self._render_scene(t_sample)

    self._motion_blur.render_with_blur(
        render_scene_at_time, t_base, dt, chart_speed, samples, shutter
    )

    # Render effects on top
    self._draw_particles()
    # ... other overlays ...
```

### Phase 8: Optional - Integrate GPU Features

#### Texture Manager Integration

```python
def __post_init__(self):
    # ... existing init ...

    # Initialize texture manager when respack is available
    def init_texture_manager(self, respack):
        """Initialize texture manager from respack."""
        if self._texture_manager is None:
            self._texture_manager = create_texture_manager_from_respack(
                self.ctx, respack,
                layer_size=(256, 256),
                max_layers=64
            )
            print(f"Texture manager initialized: {self._texture_manager.get_stats()}")
```

#### Batch Renderer Integration

```python
def __post_init__(self):
    # ... existing init ...

    def init_batch_renderer(self):
        """Initialize batch renderer (requires texture manager)."""
        if self._batch_renderer is None and self._texture_manager is not None:
            self._batch_renderer = InstancedBatchRenderer(
                self.ctx, self._texture_manager,
                max_instances=10000
            )
            self._batch_renderer.set_window_size(*self.window_size)
            print("Batch renderer initialized")
```

#### Trail Effect Integration

```python
def __post_init__(self):
    # ... existing init ...

    def init_trail_effect(self):
        """Initialize GPU trail effect."""
        W, H = self.window_size
        self._trail_effect = GPUTrailEffect(self.ctx, W, H, max_frames=8)
        print(f"Trail effect initialized: {self._trail_effect.get_stats()}")

def render(self, dt: float) -> None:
    # ... existing setup ...

    # Check if trail is enabled
    trail_config = self.render_ctx.get("trail_config", None)
    if trail_config and self._trail_effect:
        # Render to offscreen FBO
        # (requires FBO setup - omitted for brevity)
        self._trail_effect.capture_frame(current_fbo)
        self._trail_effect.render_trail(
            screen_fbo,
            alpha=trail_config.get('alpha', 0.8),
            decay=trail_config.get('decay', 0.9),
            blur_amount=trail_config.get('blur', 1.0),
            additive=trail_config.get('additive', False)
        )
```

## Configuration Options

### Enable Batch Renderer

```python
# In args or config:
args.use_batch_renderer = True

# In render code:
if getattr(self.args, "use_batch_renderer", False) and self._batch_renderer:
    # Use batch renderer path
    for note in visible_notes:
        self._batch_renderer.add_sprite(...)
    self._batch_renderer.flush()
else:
    # Use traditional renderer
    # ... existing code ...
```

### Enable GPU Trail Effect

```python
# In render_ctx:
render_ctx["trail_config"] = {
    'enabled': True,
    'alpha': 0.8,
    'decay': 0.9,
    'blur': 1.0,
    'additive': False,
}
```

## Testing Checklist

After integration, verify:

- [ ] **Font cache**: Text renders correctly on lines
- [ ] **Background**: Images load and display with dim overlay
- [ ] **Lines**: Judgment lines render with correct position/rotation/color
- [ ] **Notes**: All note types render (tap, drag, hold, flick)
- [ ] **Holds**: 3-slice holds render with head/mid/tail sections
- [ ] **Particles**: Particle bursts appear on note hits
- [ ] **Hit effects**: Circles or sprite sheets appear on hits
- [ ] **Motion blur**: Multi-sampling works with motion blur enabled
- [ ] **Visual parity**: Output matches existing implementation

## Performance Validation

Compare FPS before/after:

```python
# Add to render():
import time
frame_start = time.perf_counter()

# ... render frame ...

frame_time = (time.perf_counter() - frame_start) * 1000
print(f"Frame time: {frame_time:.2f}ms ({1000/frame_time:.1f} FPS)")
```

Expected improvements with GPU features:
- **Texture arrays**: +30-50% FPS
- **Instanced rendering**: +2-3x FPS (sprite-heavy scenes)
- **GPU trails**: +5-10x trail render speed

## Troubleshooting

### Import Errors

Ensure all `__init__.py` files exist:
```bash
touch rendering/__init__.py
touch effects/__init__.py
touch hold/__init__.py
touch resources/__init__.py
```

### Missing Methods

If a renderer method is not found, check:
1. Module is imported correctly
2. Instance is initialized in `__post_init__()`
3. Method signature matches expectations

### Performance Regression

If performance is worse:
1. Check if GPU features are actually being used
2. Verify texture arrays are bound correctly
3. Ensure batch renderer is flushing per-frame
4. Profile with: `python -m cProfile ...`

## Next Steps

1. **Immediate**: Integrate font cache, background, and frame renderer
2. **Short-term**: Integrate all effects (particles, hit FX, motion blur)
3. **Medium-term**: Test visual parity with original implementation
4. **Long-term**: Enable GPU features (texture arrays, instancing, trails)
5. **Final**: Benchmark and optimize

## Example: Minimal Integration

Quickest path to get new architecture working:

```python
# In GLApp.__post_init__():
self._font_cache = FontCache(self.ctx)
self._particle_system = ParticleSystem(self.ctx)
self._hitfx_manager = HitEffectManager(self.ctx)
self._hold_renderer = HoldRenderer(self.ctx, self.sprite, self.window_size)
self._line_renderer = LineRenderer(self.ctx, self.window_size, self._font_cache)
self._note_renderer = NoteRenderer(self.ctx, self.window_size, self._hold_renderer)

# Replace text rendering:
# self._get_text_texture(...) → self._font_cache.get_text_texture(...)

# Done! Test with existing app flow.
```

## Conclusion

All modules are ready. Integration is straightforward:
1. Initialize modules in `__post_init__()`
2. Replace method calls with module equivalents
3. Test incrementally
4. Enable GPU features for performance boost

The modular architecture is complete and waiting for integration!
