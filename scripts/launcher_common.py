"""Compatibility shim.

Shared launcher primitives moved to :mod:`phigros_ui.common`.  This module
re-exports them so ``scripts/build.py`` and any third-party scripts that
imported from ``launcher_common`` continue to work unchanged.
"""

from __future__ import annotations

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from phigros_ui.common import (  # noqa: E402,F401
    BUILD_PROFILES,
    CONFIG_DIR,
    CPP_DIR,
    DEFAULT_BINARY,
    DEFAULT_CHARTS_DIR,
    DEFAULT_CONFIG,
    DEFAULT_RESPACK,
    INTERNAL_ASSET_LABEL,
    ROOT_DIR,
    BuildRequest,
    RendererOptions,
    build_commands,
    build_output_dir,
    clipboard_copy,
    cpu_jobs,
    discover_charts,
    format_command,
    is_internal_asset_path,
    launch_binary_command,
    renderer_command,
    run_commands,
)
