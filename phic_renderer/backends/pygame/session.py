"""Pygame session implementation.

This module provides the PygameSession class, which encapsulates a single
Pygame rendering session with proper resource management.

Phase 4 Goal: Break down the monolithic pygame_backend.py::run() into:
- PygameSession (coordinator)
- GameLoop (main loop logic)
- InputHandler (pointer/keyboard input)
- JudgeEngine (judgment orchestration)
- FrameBuilder (frame assembly)
"""

from __future__ import annotations
from typing import Any, List, Dict, Optional
import logging

from ...core.session import GameSession
from ...core.context import ResourceContext
from ...config.schema import RenderConfig
from ...types import RuntimeLine, RuntimeNote


class PygameSession(GameSession):
    """Pygame-specific game session.

    Currently a wrapper around the existing run() function.
    Will be refactored to use modular components in future iterations.
    """

    def __init__(
        self,
        config: RenderConfig,
        resources: ResourceContext,
        lines: List[RuntimeLine],
        notes: List[RuntimeNote],
        chart_info: Dict[str, Any],
        **kwargs: Any,
    ):
        """Initialize Pygame session.

        Args:
            config: Rendering configuration
            resources: Resource context
            lines: Judgment lines
            notes: Chart notes
            chart_info: Chart metadata
            **kwargs: Additional parameters (W, H, offset, etc.)
        """
        super().__init__(config, resources, lines, notes, chart_info, **kwargs)
        self.screen: Optional[Any] = None
        self._initialized = False

    def initialize(self) -> None:
        """Initialize Pygame backend.

        Sets up:
        - Pygame display
        - Audio backend
        - Resource caches
        - Subsystems
        """
        import pygame

        if not pygame.get_init():
            pygame.init()

        # Screen dimensions from kwargs
        W = self.kwargs.get("W", 1280)
        H = self.kwargs.get("H", 720)

        self.screen = pygame.display.set_mode((W, H))
        pygame.display.set_caption("PhicRenderer")

        # Initialize session-scoped resources
        # TODO: Replace global singletons with session-scoped resources
        # self.resources.surface_pool = SurfacePool()
        # self.resources.transform_cache = TransformCache()
        # etc.

        self._initialized = True
        self._logger.info("Pygame session initialized: %dx%d", W, H)

    def run_game_loop(self) -> Any:
        """Run the main game loop.

        Currently delegates to the existing pygame_backend.run() function.
        Future: Will use modular GameLoop class.

        Returns:
            Game result (stats, etc.)
        """
        # For now, delegate to existing implementation
        # This maintains all existing functionality while we build the new structure
        from ...renderer.pygame_backend import run

        # Create a mock args object from config and kwargs
        # This bridges the new config-based approach with the old args-based code
        class MockArgs:
            def __init__(self, config: RenderConfig, kwargs: Dict[str, Any]):
                # Copy all config values as attributes
                self.expand = config.expand_factor
                self.note_scale_x = config.note_scale_x
                self.note_scale_y = config.note_scale_y
                self.note_flow_speed_multiplier = config.note_flow_speed_multiplier
                self.note_speed_mul_affects_travel = config.note_speed_mul_affects_travel
                self.force_line_alpha01 = config.force_line_alpha01
                self.force_line_alpha01_by_lid = config.force_line_alpha01_by_lid
                self.overrender = config.render_overrender
                self.trail_alpha = config.trail_alpha
                self.trail_frames = config.trail_frames
                self.trail_decay = config.trail_decay
                self.trail_blur = config.trail_blur
                self.trail_dim = config.trail_dim
                self.motion_blur_samples = config.motion_blur_samples
                self.motion_blur_shutter = config.motion_blur_shutter

                # Copy all kwargs as attributes
                for key, value in kwargs.items():
                    setattr(self, key, value)

        args = MockArgs(self.config, self.kwargs)

        # Call existing run() function
        result = run(
            args,
            W=self.kwargs.get("W", 1280),
            H=self.kwargs.get("H", 720),
            expand=self.config.expand_factor,
            fmt=self.kwargs.get("fmt", "official"),
            offset=self.kwargs.get("offset", 0.0),
            lines=self.lines,
            notes=self.notes,
            chart_info=self.chart_info,
            bg_dim_alpha=self.kwargs.get("bg_dim_alpha"),
            bg_path=self.kwargs.get("bg_path"),
            music_path=self.kwargs.get("music_path"),
            chart_path=self.kwargs.get("chart_path"),
            advance_active=self.kwargs.get("advance_active", False),
            advance_cfg=self.kwargs.get("advance_cfg"),
            advance_mix=self.kwargs.get("advance_mix", False),
            advance_tracks_bgm=self.kwargs.get("advance_tracks_bgm", []),
            advance_main_bgm=self.kwargs.get("advance_main_bgm"),
            advance_segment_starts=self.kwargs.get("advance_segment_starts", []),
            advance_segment_bgm=self.kwargs.get("advance_segment_bgm", []),
            advance_base_dir=self.kwargs.get("advance_base_dir"),
            reuse_pygame=True,  # We already initialized it
        )

        return result

    def cleanup(self) -> None:
        """Clean up Pygame resources."""
        if self._initialized:
            self.resources.cleanup()
            self._logger.info("Pygame session cleaned up")


__all__ = ["PygameSession"]
