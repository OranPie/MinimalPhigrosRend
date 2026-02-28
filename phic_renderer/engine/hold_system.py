"""Unified hold note system.

This module consolidates hold note functionality previously scattered across:
- renderer/pygame/hold.py (rendering)
- renderer/pygame/hold_logic.py (game logic)
- renderer/pygame/hold_cache.py (caching)

The HoldSystem class provides a clean interface for hold note management,
rendering, and performance optimization.
"""

from __future__ import annotations
from typing import Any, List, Callable
import logging

# Re-export the cache class for backward compatibility
from ..backends.pygame.hold.cache import HoldCache

# Import the original functions (will be wrapped)
from ..backends.pygame.hold.render import draw_hold_3slice as _draw_hold_3slice
from ..backends.pygame.hold.logic import (
    hold_maintenance as _hold_maintenance,
    hold_finalize as _hold_finalize,
    hold_tick_fx as _hold_tick_fx,
)


class HoldSystem:
    """Unified hold note system.

    Manages hold note lifecycle, rendering, and caching. This class consolidates
    three previously separate modules into a single cohesive interface.

    Attributes:
        cache: LRU cache for pre-rendered hold segments
    """

    def __init__(self, cache_size: int = 200):
        """Initialize hold system.

        Args:
            cache_size: Maximum number of cached hold segments
        """
        self.cache = HoldCache(max_entries=cache_size)
        self._logger = logging.getLogger(__name__)

    def maintenance(
        self,
        *,
        args: Any,
        states: List[Any],
        idx_next: int,
        t: float,
        hold_tail_tol: float,
        W: int,
        H: int,
        lines: List[Any],
        pointers: Any,
        judge: Any,
    ) -> None:
        """Update hold note states during gameplay.

        Checks if fingers are still holding notes and updates state accordingly.
        Marks holds as released early if finger lifts.

        Args:
            args: Argument namespace (for judge_width, judge_height)
            states: List of note states
            idx_next: Index of next note to be judged
            t: Current game time
            hold_tail_tol: Tolerance for hold tail (e.g., 0.3 = 30%)
            W: Screen width
            H: Screen height
            lines: Judgment lines
            pointers: Pointer manager
            judge: Judge instance
        """
        _hold_maintenance(
            args=args,
            states=states,
            idx_next=idx_next,
            t=t,
            hold_tail_tol=hold_tail_tol,
            W=W,
            H=H,
            lines=lines,
            pointers=pointers,
            judge=judge,
        )

    def finalize(
        self,
        *,
        states: List[Any],
        idx_next: int,
        t: float,
        hold_tail_tol: float,
        miss_window: float,
        judge: Any,
        push_hit_debug_cb: Callable[..., Any],
    ) -> None:
        """Finalize completed hold notes.

        Calculates final score for hold notes that have ended and updates
        judge statistics.

        Args:
            states: List of note states
            idx_next: Index of next note to be judged
            t: Current game time
            hold_tail_tol: Tolerance for hold tail
            miss_window: Time window for miss judgment
            judge: Judge instance
            push_hit_debug_cb: Callback for debug info
        """
        _hold_finalize(
            states=states,
            idx_next=idx_next,
            t=t,
            hold_tail_tol=hold_tail_tol,
            miss_window=miss_window,
            judge=judge,
            push_hit_debug_cb=push_hit_debug_cb,
        )

    def tick_effects(
        self,
        *,
        states: List[Any],
        idx_next: int,
        t: float,
        hold_fx_interval_ms: int,
        lines: List[Any],
        respack: Any,
        hitfx: List[Any],
        particles: List[Any],
        HitFX_cls: Any,
        ParticleBurst_cls: Any,
        mark_line_hit_cb: Callable[[int, int], Any],
    ) -> None:
        """Generate visual/audio effects for active hold notes.

        Creates periodic hit effects and particles while hold notes are being held.

        Args:
            states: List of note states
            idx_next: Index of next note to be judged
            t: Current game time
            hold_fx_interval_ms: Interval between effects (milliseconds)
            lines: Judgment lines
            respack: Resource pack
            hitfx: List to append hit effects to
            particles: List to append particle bursts to
            HitFX_cls: HitFX class constructor
            ParticleBurst_cls: ParticleBurst class constructor
            mark_line_hit_cb: Callback to mark line as hit
        """
        _hold_tick_fx(
            states=states,
            idx_next=idx_next,
            t=t,
            hold_fx_interval_ms=hold_fx_interval_ms,
            lines=lines,
            respack=respack,
            hitfx=hitfx,
            particles=particles,
            HitFX_cls=HitFX_cls,
            ParticleBurst_cls=ParticleBurst_cls,
            mark_line_hit_cb=mark_line_hit_cb,
        )

    def draw(
        self,
        overlay: Any,
        head_xy: tuple[float, float],
        tail_xy: tuple[float, float],
        line_rot: float,
        alpha01: float,
        line_rgb: tuple[int, int, int],
        note_rgb: tuple[int, int, int],
        size_scale: float,
        mh: bool,
        hold_body_w: int,
        progress: float | None = None,
        draw_outline: bool = True,
        outline_width: int = 2,
    ) -> None:
        """Render a hold note using 3-slice technique.

        Args:
            overlay: Surface to draw on
            head_xy: Head position (x, y)
            tail_xy: Tail position (x, y)
            line_rot: Line rotation
            alpha01: Alpha (0-1)
            line_rgb: Line color
            note_rgb: Note tint color
            size_scale: Size scaling
            mh: Multi-highlight mode
            hold_body_w: Hold body width
            progress: Hold progress (0-1) or None
            draw_outline: Whether to draw outline
            outline_width: Outline width
        """
        _draw_hold_3slice(
            overlay=overlay,
            head_xy=head_xy,
            tail_xy=tail_xy,
            line_rot=line_rot,
            alpha01=alpha01,
            line_rgb=line_rgb,
            note_rgb=note_rgb,
            size_scale=size_scale,
            mh=mh,
            hold_body_w=hold_body_w,
            progress=progress,
            draw_outline=draw_outline,
            outline_width=outline_width,
        )

    def get_cache_stats(self) -> dict[str, int]:
        """Get cache statistics.

        Returns:
            Dictionary with 'hits' and 'misses' counts
        """
        return {
            "hits": self.cache.stats_hits,
            "misses": self.cache.stats_misses,
        }

    def clear_cache(self) -> None:
        """Clear the hold rendering cache."""
        self.cache.clear()
        self._logger.debug("Hold cache cleared")


# For backward compatibility, export the cache class
__all__ = ["HoldSystem", "HoldCache"]
