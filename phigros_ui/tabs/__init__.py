"""Tab widgets for the phigros_ui main window.

Each submodule exports a single ``QWidget`` subclass that is mounted as a tab
by :class:`phigros_ui.app.LauncherWindow`.  New tabs should register
themselves here so the main window stays declarative.
"""

from __future__ import annotations

from .build import BuildTab
from .config_editor import ConfigEditorTab
from .log import LogTab
from .renderer import RendererTab

__all__ = ["BuildTab", "ConfigEditorTab", "LogTab", "RendererTab"]
