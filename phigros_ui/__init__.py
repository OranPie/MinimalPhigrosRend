"""MinimalPhigrosRend desktop UI.

A PySide6 application that wraps the native renderer, build system and
chart tooling in a single window.  Launch via ``python -m phigros_ui`` or
``python3 scripts/qt_launcher.py`` (the latter is a compatibility shim).
"""

from __future__ import annotations

__version__ = "0.2.0"

from .common import (
    BUILD_PROFILES,
    DEFAULT_BINARY,
    DEFAULT_CHARTS_DIR,
    DEFAULT_CONFIG,
    DEFAULT_RESPACK,
    INTERNAL_ASSET_LABEL,
    ROOT_DIR,
    BuildRequest,
    RendererOptions,
    build_commands,
    discover_charts,
    format_command,
    launch_binary_command,
)

__all__ = [
    "__version__",
    "BUILD_PROFILES",
    "DEFAULT_BINARY",
    "DEFAULT_CHARTS_DIR",
    "DEFAULT_CONFIG",
    "DEFAULT_RESPACK",
    "INTERNAL_ASSET_LABEL",
    "ROOT_DIR",
    "BuildRequest",
    "RendererOptions",
    "build_commands",
    "discover_charts",
    "format_command",
    "launch_binary_command",
]
