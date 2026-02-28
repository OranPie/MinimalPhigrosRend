"""Base game session abstraction.

This module provides the GameSession abstract base class that defines the
interface for backend-specific session implementations (Pygame, ModernGL).
"""

from __future__ import annotations
from abc import ABC, abstractmethod
from typing import Any, Optional, List, Dict
import logging

from .context import ResourceContext
from ..config.schema import RenderConfig
from ..types import RuntimeLine, RuntimeNote


class GameSession(ABC):
    """Base class for backend-specific game sessions.

    A GameSession represents a single rendering session, owning all resources
    and subsystems needed for that session. Different backends (Pygame, ModernGL)
    implement this interface with backend-specific logic.

    This replaces the monolithic run() function pattern with a class-based
    design that supports better state management and testing.
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
        """Initialize game session.

        Args:
            config: Immutable rendering configuration
            resources: Session-scoped resource context
            lines: Judgment lines from chart
            notes: Notes from chart
            chart_info: Chart metadata
            **kwargs: Additional backend-specific parameters
        """
        self.config = config
        self.resources = resources
        self.lines = lines
        self.notes = notes
        self.chart_info = chart_info
        self.kwargs = kwargs

        self._logger = logging.getLogger(self.__class__.__name__)

        # Subsystems (initialized by subclass)
        self.note_manager: Optional[Any] = None
        self.hold_system: Optional[Any] = None
        self.judge_engine: Optional[Any] = None
        self.input_handler: Optional[Any] = None

    @abstractmethod
    def initialize(self) -> None:
        """Initialize backend-specific resources and subsystems.

        This should set up the rendering backend, create subsystems
        (note_manager, hold_system, judge_engine, input_handler), and
        prepare for the game loop.

        Raises:
            Any backend-specific initialization errors
        """
        pass

    @abstractmethod
    def run_game_loop(self) -> Any:
        """Run the main game loop.

        This contains the core rendering and update logic. The loop should
        continue until the game ends (chart completes, user quits, etc.).

        Returns:
            Backend-specific return value (e.g., final stats, exit code)
        """
        pass

    @abstractmethod
    def cleanup(self) -> None:
        """Clean up resources.

        Release all resources owned by this session, including:
        - Rendering backend resources
        - Audio resources
        - Cached surfaces/textures
        - Any other allocated memory

        This should be called when the session ends, either normally
        or due to an error.
        """
        pass

    def run(self) -> Any:
        """Convenience method to run full session lifecycle.

        Calls initialize(), run_game_loop(), and cleanup() in sequence,
        ensuring cleanup happens even if an error occurs.

        Returns:
            Result from run_game_loop()

        Raises:
            Any errors from initialization or game loop
        """
        try:
            self._logger.info("Initializing session")
            self.initialize()

            self._logger.info("Starting game loop")
            result = self.run_game_loop()

            self._logger.info("Game loop completed")
            return result
        finally:
            self._logger.info("Cleaning up session")
            self.cleanup()
