"""
Helper utilities for initializing ModernGL rendering subsystems.

Simplifies setup of all modular components.
"""

from __future__ import annotations

from typing import Any, Optional
from dataclasses import dataclass

from .resources.font_cache import FontCache
from .resources.background import BackgroundManager
from .resources.texture_manager import GLTextureManager, create_texture_manager_from_respack
from .effects.particles import ParticleSystem
from .effects.hitfx import HitEffectManager
from .effects.motion_blur import MotionBlurEffect
from .effects.trail import GPUTrailEffect
from .hold.renderer import HoldRenderer
from .rendering.notes import NoteRenderer
from .rendering.lines import LineRenderer
from .rendering.frame_renderer import FrameRenderer
from .rendering.batch import InstancedBatchRenderer


@dataclass
class RenderingSubsystems:
    """Container for all rendering subsystems."""

    # Resources
    font_cache: FontCache
    bg_manager: BackgroundManager
    texture_manager: Optional[GLTextureManager]

    # Effects
    particle_system: ParticleSystem
    hitfx_manager: HitEffectManager
    motion_blur: MotionBlurEffect
    trail_effect: Optional[GPUTrailEffect]

    # Rendering
    hold_renderer: HoldRenderer
    note_renderer: NoteRenderer
    line_renderer: LineRenderer
    frame_renderer: FrameRenderer

    # Optional GPU features
    batch_renderer: Optional[InstancedBatchRenderer]


def initialize_rendering_subsystems(
    ctx: Any,
    sprite_program: Any,
    r2d: Any,
    window_size: tuple[int, int],
    enable_texture_manager: bool = False,
    enable_batch_renderer: bool = False,
    enable_trail_effect: bool = False,
    trail_max_frames: int = 8,
) -> RenderingSubsystems:
    """
    Initialize all rendering subsystems.

    Args:
        ctx: ModernGL context
        sprite_program: Sprite rendering program
        r2d: 2D geometry renderer
        window_size: (width, height) tuple
        enable_texture_manager: Enable GPU texture arrays
        enable_batch_renderer: Enable instanced batch renderer (requires texture_manager)
        enable_trail_effect: Enable GPU trail effect
        trail_max_frames: Maximum frames for trail history

    Returns:
        RenderingSubsystems with all modules initialized
    """
    W, H = window_size

    # Resources
    font_cache = FontCache(ctx, max_cache_size=128)
    bg_manager = BackgroundManager(ctx)

    texture_manager = None
    if enable_texture_manager:
        # Will be populated when respack is loaded
        texture_manager = GLTextureManager(ctx, layer_size=(256, 256), max_layers=64)

    # Effects
    particle_system = ParticleSystem(ctx)
    hitfx_manager = HitEffectManager(ctx)
    motion_blur = MotionBlurEffect(ctx)

    trail_effect = None
    if enable_trail_effect:
        trail_effect = GPUTrailEffect(ctx, W, H, max_frames=trail_max_frames)

    # Hold renderer
    hold_renderer = HoldRenderer(ctx, sprite_program, window_size)

    # Note and line renderers
    line_renderer = LineRenderer(ctx, window_size, font_cache)
    note_renderer = NoteRenderer(ctx, window_size, hold_renderer)

    # Frame renderer coordinator
    frame_renderer = FrameRenderer(
        ctx, sprite_program, r2d, window_size,
        line_renderer, note_renderer,
        particle_system, hitfx_manager
    )

    # Optional: Batch renderer (requires texture manager)
    batch_renderer = None
    if enable_batch_renderer and texture_manager is not None:
        batch_renderer = InstancedBatchRenderer(ctx, texture_manager, max_instances=10000)
        batch_renderer.set_window_size(W, H)

    return RenderingSubsystems(
        font_cache=font_cache,
        bg_manager=bg_manager,
        texture_manager=texture_manager,
        particle_system=particle_system,
        hitfx_manager=hitfx_manager,
        motion_blur=motion_blur,
        trail_effect=trail_effect,
        hold_renderer=hold_renderer,
        note_renderer=note_renderer,
        line_renderer=line_renderer,
        frame_renderer=frame_renderer,
        batch_renderer=batch_renderer,
    )


def load_respack_into_texture_manager(
    texture_manager: GLTextureManager,
    respack: Any,
    texture_names: Optional[list[str]] = None,
) -> dict[str, int]:
    """
    Load respack textures into texture manager.

    Args:
        texture_manager: GLTextureManager instance
        respack: Resource pack with images
        texture_names: Optional list of texture names to load (defaults to standard notes)

    Returns:
        Dictionary mapping texture names to layer indices
    """
    if texture_names is None:
        # Standard note textures
        texture_names = [
            "click.png",
            "click_mh.png",
            "drag.png",
            "drag_mh.png",
            "flick.png",
            "flick_mh.png",
            "hold.png",
            "hold_mh.png",
        ]

    return texture_manager.load_respack_textures(respack, texture_names)


def get_subsystem_stats(subsystems: RenderingSubsystems) -> dict:
    """
    Get statistics from all subsystems.

    Args:
        subsystems: RenderingSubsystems instance

    Returns:
        Dictionary with stats from all subsystems
    """
    stats = {}

    # Font cache
    stats['font_cache'] = subsystems.font_cache.get_stats()

    # Background manager
    stats['bg_manager'] = subsystems.bg_manager.get_stats()

    # Texture manager
    if subsystems.texture_manager:
        stats['texture_manager'] = subsystems.texture_manager.get_stats()

    # Particle system
    stats['particle_count'] = subsystems.particle_system.get_count()

    # Hit effects
    stats['hitfx_count'] = subsystems.hitfx_manager.get_count()

    # Trail effect
    if subsystems.trail_effect:
        stats['trail_effect'] = subsystems.trail_effect.get_stats()

    # Batch renderer
    if subsystems.batch_renderer:
        stats['batch_renderer'] = subsystems.batch_renderer.get_stats()

    return stats


def cleanup_subsystems(subsystems: RenderingSubsystems):
    """
    Clean up and release resources from all subsystems.

    Args:
        subsystems: RenderingSubsystems instance
    """
    # Clear caches
    subsystems.font_cache.clear()
    subsystems.bg_manager.clear()

    if subsystems.texture_manager:
        subsystems.texture_manager.clear()

    # Clear effects
    subsystems.particle_system.clear()
    subsystems.hitfx_manager.clear()

    if subsystems.trail_effect:
        subsystems.trail_effect.clear()

    if subsystems.batch_renderer:
        subsystems.batch_renderer.clear()

    # Release GPU resources
    if subsystems.texture_manager:
        subsystems.texture_manager.release()

    if subsystems.trail_effect:
        subsystems.trail_effect.release()


# Convenience function for quick setup
def quick_setup(ctx: Any, sprite_program: Any, r2d: Any, window_size: tuple) -> RenderingSubsystems:
    """
    Quick setup with default configuration (no GPU features).

    Args:
        ctx: ModernGL context
        sprite_program: Sprite rendering program
        r2d: 2D geometry renderer
        window_size: (width, height) tuple

    Returns:
        RenderingSubsystems with basic configuration
    """
    return initialize_rendering_subsystems(
        ctx, sprite_program, r2d, window_size,
        enable_texture_manager=False,
        enable_batch_renderer=False,
        enable_trail_effect=False,
    )


# Advanced setup with all GPU features
def gpu_optimized_setup(ctx: Any, sprite_program: Any, r2d: Any, window_size: tuple) -> RenderingSubsystems:
    """
    Setup with all GPU optimization features enabled.

    Args:
        ctx: ModernGL context
        sprite_program: Sprite rendering program
        r2d: 2D geometry renderer
        window_size: (width, height) tuple

    Returns:
        RenderingSubsystems with all GPU features enabled
    """
    return initialize_rendering_subsystems(
        ctx, sprite_program, r2d, window_size,
        enable_texture_manager=True,
        enable_batch_renderer=True,
        enable_trail_effect=True,
        trail_max_frames=8,
    )
