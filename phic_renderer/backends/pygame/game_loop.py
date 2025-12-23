"""Game loop extraction stub.

TODO: Extract main game loop from pygame_backend.py::run()

This module will contain the core game loop logic, including:
- Frame timing
- Update cycle
- Render cycle
- Event processing
- Recording integration

Current Status: Stub - logic still in pygame_backend.py::run()
Phase 4 Goal: Extract into clean GameLoop class
"""

from __future__ import annotations
from typing import Any


class GameLoop:
    """Main game loop coordinator (TODO: implement extraction).

    Future structure:
    - __init__(session, config, resources)
    - run() -> result
    - _update_frame(dt)
    - _render_frame()
    - _handle_events()
    """

    def __init__(self, session: Any, config: Any, resources: Any):
        self.session = session
        self.config = config
        self.resources = resources

    def run(self) -> Any:
        """Run the main game loop.

        TODO: Extract logic from pygame_backend.py
        """
        raise NotImplementedError("GameLoop extraction pending - use PygameSession for now")


__all__ = ["GameLoop"]
