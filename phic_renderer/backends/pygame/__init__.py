"""Pygame rendering backend.

This module provides the Pygame-based rendering implementation.
All implementation files are now organized in logical subdirectories under backends/pygame/.

The main entry point is still maintained in renderer/pygame_backend.py for backward compatibility,
but will eventually be refactored into PygameSession class.
"""

from __future__ import annotations

# Note: We don't re-export the run function here to avoid circular imports.
# The run function is available from phic_renderer.renderer.pygame_backend
# or will be available from phic_renderer.backends.pygame.session in the future.

__all__ = []
