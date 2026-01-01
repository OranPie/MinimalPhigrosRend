"""
Example: Integrating modular subsystems into GLApp.

This file shows how to modify app.py to use the new modular architecture.
Copy the relevant sections into your actual app.py.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List

# Import the helper
from .subsystem_init import (
    RenderingSubsystems,
    initialize_rendering_subsystems,
    load_respack_into_texture_manager,
    get_subsystem_stats,
)

from .effects.motion_blur import get_motion_blur_config
from ...types import NoteState
from ...engine.judge import Judge


@dataclass
class GLAppModular:
    """
    Example GLApp with modular architecture.

    This shows the structure after integration. Copy relevant parts
    to your actual GLApp class in app.py.
    """

    ctx: Any
    window_size: tuple[int, int]
    r2d: Any
    sprite: Any
    args: Any
    render_ctx: Dict[str, Any]
    t0: float

    # NEW: Single subsystems container instead of many fields
    subsystems: RenderingSubsystems = field(default=None)

    # Keep existing game state fields
    _states: List[NoteState] = field(default_factory=list)
    _judge: Any = None
    _down: bool = False
    _press_edge: bool = False
    _line_last_hit_ms: Dict[int, int] = field(default_factory=dict)
    _prev_tick_ms: int = 0
    _tick_acc_ms: float = 0.0
    _last_tick_ms: int = 0
    _fps_smooth: float = 0.0
    _note_render_count_last: int = 0

    def __post_init__(self):
        """Initialize all subsystems using the helper."""

        # Option 1: Quick setup (basic features only)
        # self.subsystems = quick_setup(self.ctx, self.sprite, self.r2d, self.window_size)

        # Option 2: Custom configuration
        enable_gpu_features = getattr(self.args, "use_gpu_features", False)

        self.subsystems = initialize_rendering_subsystems(
            self.ctx,
            self.sprite,
            self.r2d,
            self.window_size,
            enable_texture_manager=enable_gpu_features,
            enable_batch_renderer=enable_gpu_features,
            enable_trail_effect=getattr(self.args, "enable_trails", False),
        )

        print("✅ Modular subsystems initialized")

        # Load respack textures if texture manager enabled
        respack = self.render_ctx.get("respack", None)
        if respack and self.subsystems.texture_manager:
            result = load_respack_into_texture_manager(
                self.subsystems.texture_manager,
                respack
            )
            print(f"✅ Loaded {len(result)} textures into texture manager")

    # ==================================================================
    # EXAMPLE: Simplified render() method
    # ==================================================================

    def render(self, dt: float) -> None:
        """Render a frame using modular subsystems."""

        state = self.render_ctx.get("state", None)
        chart_speed = float(getattr(self.args, "chart_speed", 1.0) or 1.0)

        # Calculate time (existing logic)
        t_base = float(self.t0) + sum(...)  # Your time calculation

        # Update effects
        self._update_effects(t_base, dt)

        # Get motion blur config
        samples, shutter = get_motion_blur_config(state)

        # Clear screen
        self.ctx.clear(0.06, 0.06, 0.08, 1.0)

        # Render with motion blur
        def render_scene_at_time(t_sample: float, weight: float):
            self._set_weight(weight)
            self._render_scene(t_sample)

        self.subsystems.motion_blur.render_with_blur(
            render_scene_at_time, t_base, dt, chart_speed, samples, shutter
        )

        # Render particles on top
        self._draw_particles()

        # Debug overlays
        if getattr(self.args, "basic_debug", False):
            self._draw_basic_debug()

    # ==================================================================
    # EXAMPLE: Simplified _render_scene() method
    # ==================================================================

    def _render_scene(self, t: float) -> None:
        """Render scene using frame renderer."""

        # Use frame renderer for main rendering
        self._ensure_judge_state()

        note_render_count = self.subsystems.frame_renderer.render_frame(
            t, self.render_ctx, self._states, self.args
        )

        self._note_render_count_last = note_render_count

    # ==================================================================
    # EXAMPLE: Simplified _update_effects() method
    # ==================================================================

    def _update_effects(self, t: float, dt: float) -> None:
        """Update all effects using subsystems."""

        respack = self.render_ctx.get("respack", None)
        now_tick = self._now_tick_ms(float(dt))
        self._last_tick_ms = int(now_tick)

        # Update particles
        self.subsystems.particle_system.update(now_tick)

        # Update hit effects
        self.subsystems.hitfx_manager.update(t, respack)

        # Autoplay logic (if enabled)
        if bool(getattr(self.args, "autoplay", False)):
            self._process_autoplay(t, now_tick, respack)

    # ==================================================================
    # EXAMPLE: Simplified _draw_particles() method
    # ==================================================================

    def _draw_particles(self) -> None:
        """Render particles using particle system."""

        respack = self.render_ctx.get("respack", None)
        expand = float(self.render_ctx.get("expand") or 1.0)
        now_tick = int(getattr(self, "_last_tick_ms", 0) or 0)

        self.subsystems.frame_renderer.render_particles(now_tick, expand, respack)

    # ==================================================================
    # EXAMPLE: Autoplay with modular effects
    # ==================================================================

    def _process_autoplay(self, t: float, now_tick: int, respack: Any) -> None:
        """Process autoplay hits (example showing how to add effects)."""

        # ... existing autoplay logic ...

        # When a note is hit:
        # x, y = note position
        # color = note color
        # lr = line rotation

        # Add hit effect
        # self.subsystems.hitfx_manager.add_effect(x, y, t, color, lr, variant="")

        # Add particle burst
        # if respack and not getattr(respack, "hide_particles", False):
        #     duration = int(float(respack.hitfx_duration) * 1000.0)
        #     self.subsystems.particle_system.add_burst(x, y, now_tick, duration, color)

    # ==================================================================
    # EXAMPLE: Cleanup
    # ==================================================================

    def cleanup(self):
        """Clean up all subsystems."""
        from .subsystem_init import cleanup_subsystems
        cleanup_subsystems(self.subsystems)
        print("✅ Subsystems cleaned up")

    # ==================================================================
    # EXAMPLE: Get stats
    # ==================================================================

    def get_rendering_stats(self) -> dict:
        """Get statistics from all rendering subsystems."""
        return get_subsystem_stats(self.subsystems)

    # ==================================================================
    # Helper methods (keep from original)
    # ==================================================================

    def _set_weight(self, w: float) -> None:
        """Set rendering weight for motion blur."""
        try:
            self.r2d.prog["u_weight"].value = float(w)
        except:
            pass

    def set_input(self, *, down: bool, press_edge: bool) -> None:
        """Set input state."""
        self._down = bool(down)
        self._press_edge = bool(press_edge)

    def _now_tick_ms(self, dt: float) -> int:
        """Calculate current tick time in milliseconds."""
        if self._prev_tick_ms <= 0:
            self._prev_tick_ms = 0
            self._tick_acc_ms = 0.0
        self._tick_acc_ms += float(dt) * 1000.0
        if self._tick_acc_ms < 0.0:
            self._tick_acc_ms = 0.0
        self._prev_tick_ms += int(self._tick_acc_ms)
        self._tick_acc_ms = float(self._tick_acc_ms) - float(int(self._tick_acc_ms))
        return int(self._prev_tick_ms)

    def _mark_line_hit(self, lid: int, now_tick: int) -> None:
        """Mark a line as hit."""
        self._line_last_hit_ms[int(lid)] = int(now_tick)

    def _ensure_judge_state(self) -> None:
        """Ensure judge and states are initialized."""
        if self._judge is None:
            self._judge = Judge()
        if not self._states:
            notes = self.render_ctx.get("notes") or []
            self._states = [NoteState(n) for n in notes]

    def _draw_basic_debug(self) -> None:
        """Draw basic debug info (FPS, note count)."""
        # ... existing debug rendering ...
        pass


# ==================================================================
# MIGRATION CHECKLIST
# ==================================================================

"""
To migrate your existing app.py to use modular subsystems:

1. ✅ Add import for subsystem_init

2. ✅ Add `subsystems: RenderingSubsystems` field to GLApp

3. ✅ In __post_init__(), initialize subsystems:
   self.subsystems = initialize_rendering_subsystems(...)

4. ✅ Replace text rendering:
   OLD: self._get_text_texture(...)
   NEW: self.subsystems.font_cache.get_text_texture(...)

5. ✅ Replace _render_scene():
   OLD: 200+ lines of rendering code
   NEW: self.subsystems.frame_renderer.render_frame(...)

6. ✅ Replace _update_hitfx():
   OLD: Inline hit effect management
   NEW: self.subsystems.hitfx_manager.update(...)
        self.subsystems.hitfx_manager.add_effect(...)

7. ✅ Replace _draw_particles():
   OLD: Inline particle rendering
   NEW: self.subsystems.frame_renderer.render_particles(...)

8. ✅ Update motion blur:
   OLD: Manual sample loop
   NEW: self.subsystems.motion_blur.render_with_blur(...)

9. ✅ Remove old fields:
   DELETE: _text_cache, _text_cache_order
   DELETE: _hitfx, _hitfx_fired
   DELETE: _particles
   (These are now in subsystems)

10. ✅ Test incrementally - each change should work independently
"""
