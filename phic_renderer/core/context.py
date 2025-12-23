"""Resource management context for game sessions.

This module provides ResourceContext for managing session-scoped resources
like loaded respacks, fonts, rendering caches, etc. This replaces global
singleton access patterns.
"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Any, Optional, Dict
import logging


@dataclass
class ResourceContext:
    """Session-scoped resource ownership.

    Manages all resources for a single game session, replacing global
    singleton patterns (e.g., global surface pools, transform caches).
    """

    # Asset resources
    respack: Optional[Any] = None  # Loaded respack instance
    fonts: Dict[str, Any] = field(default_factory=dict)
    background: Optional[Any] = None
    audio_backend: Optional[Any] = None

    # Rendering resources (backend-specific)
    surface_pool: Optional[Any] = None
    transform_cache: Optional[Any] = None
    texture_atlas: Optional[Any] = None
    texture_map: Optional[Any] = None
    batch_renderer: Optional[Any] = None

    # Cached objects
    _logger: Optional[logging.Logger] = field(default=None, repr=False)

    def __post_init__(self):
        """Initialize logger."""
        if self._logger is None:
            object.__setattr__(self, "_logger", logging.getLogger(__name__))

    def cleanup(self):
        """Release all resources.

        Call this when the session ends to free memory and release handles.
        """
        if self._logger:
            self._logger.debug("Cleaning up resource context")

        # Clean up rendering resources
        if self.surface_pool is not None:
            if hasattr(self.surface_pool, "cleanup"):
                self.surface_pool.cleanup()

        if self.transform_cache is not None:
            if hasattr(self.transform_cache, "clear"):
                self.transform_cache.clear()

        if self.texture_atlas is not None:
            if hasattr(self.texture_atlas, "cleanup"):
                self.texture_atlas.cleanup()

        if self.batch_renderer is not None:
            if hasattr(self.batch_renderer, "cleanup"):
                self.batch_renderer.cleanup()

        # Clean up audio
        if self.audio_backend is not None:
            if hasattr(self.audio_backend, "cleanup"):
                self.audio_backend.cleanup()

    @classmethod
    def from_state_module(cls, state: Any) -> ResourceContext:
        """Create ResourceContext from legacy state module.

        This is a compatibility helper for migrating from global state.

        Args:
            state: The state module with global respack

        Returns:
            ResourceContext instance with respack from state
        """
        return cls(respack=state.respack)

    def to_state_module(self, state: Any) -> None:
        """Update legacy state module with context values.

        This is a compatibility helper for backward compatibility during migration.

        Args:
            state: The state module to update
        """
        state.respack = self.respack
