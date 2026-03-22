from __future__ import annotations

from ._compat import core

LineAlphaMode = core.LineAlphaMode
RenderConfig = core.RenderConfig
load_config = core.load_config


def config_from_dict(data: dict) -> RenderConfig:
    return core.config_from_dict(data)


def _render_config_from_dict(data: dict) -> RenderConfig:
    return config_from_dict(data)


RenderConfig.from_dict = staticmethod(_render_config_from_dict)

__all__ = ["LineAlphaMode", "RenderConfig", "config_from_dict", "load_config"]
