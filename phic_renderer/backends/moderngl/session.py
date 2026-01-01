from __future__ import annotations

from typing import Any, Optional

from ...config.schema import RenderConfig
from ...core.context import ResourceContext
from ...core.session import GameSession
from ...types import RuntimeLine, RuntimeNote


class ModernGLSession(GameSession):
    def __init__(
        self,
        config: RenderConfig,
        resources: ResourceContext,
        lines: list[RuntimeLine],
        notes: list[RuntimeNote],
        chart_info: dict[str, Any],
        **kwargs: Any,
    ):
        super().__init__(config, resources, lines, notes, chart_info, **kwargs)

        self.args = kwargs.get("args")
        self.pygame = kwargs.get("pygame")
        self.clock = kwargs.get("clock")
        self.glctx = kwargs.get("glctx")
        self.render_ctx = kwargs.get("render_ctx")
        self.create_app = kwargs.get("create_app")
        self.run_loop = kwargs.get("run_loop")
        self.window_size = kwargs.get("window_size")

        self._app: Optional[Any] = None

    def initialize(self) -> None:
        if self.create_app is None:
            raise RuntimeError("Missing create_app")
        if self.glctx is None:
            raise RuntimeError("Missing glctx")
        if self.window_size is None:
            raise RuntimeError("Missing window_size")
        self._app = self.create_app(self.glctx, window_size=self.window_size, args=self.args, render_ctx=self.render_ctx)

    def run_game_loop(self) -> Any:
        if self._app is None:
            raise RuntimeError("ModernGLSession was not initialized")

        if self.run_loop is None:
            raise RuntimeError("Missing run_loop")
        if self.pygame is None:
            raise RuntimeError("Missing pygame")
        if self.clock is None:
            raise RuntimeError("Missing clock")

        screen = None
        try:
            screen = self.pygame.display.get_surface()
        except Exception:
            screen = None

        return self.run_loop(pygame=self.pygame, clock=self.clock, screen=screen, app=self._app)

    def cleanup(self) -> None:
        try:
            self.resources.cleanup()
        except Exception:
            pass

        try:
            if self.pygame is not None:
                self.pygame.quit()
        except Exception:
            pass


__all__ = ["ModernGLSession"]
