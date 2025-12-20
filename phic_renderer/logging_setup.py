from __future__ import annotations

import logging
import os
from typing import Any, Optional


def _parse_level(s: Optional[str]) -> Optional[int]:
    if not s:
        return None
    v = str(s).strip().upper()
    if not v:
        return None
    mapping = {
        "CRITICAL": logging.CRITICAL,
        "ERROR": logging.ERROR,
        "WARNING": logging.WARNING,
        "WARN": logging.WARNING,
        "INFO": logging.INFO,
        "DEBUG": logging.DEBUG,
        "NOTSET": logging.NOTSET,
    }
    return mapping.get(v)


def setup_logging(args: Any = None, *, name: str = "phic_renderer") -> None:
    """Configure python logging once.

    Priority (highest first):
    - env PHIC_LOG_LEVEL
    - CLI flags: --quiet / --basic_debug (if present on args)
    - default: INFO
    """

    root = logging.getLogger()
    if root.handlers:
        return

    env_level = _parse_level(os.environ.get("PHIC_LOG_LEVEL"))

    quiet = bool(getattr(args, "quiet", False)) if args is not None else False
    basic_debug = bool(getattr(args, "basic_debug", False)) if args is not None else False

    level = logging.INFO
    if quiet:
        level = logging.WARNING
    if basic_debug:
        level = logging.DEBUG
    if env_level is not None:
        level = int(env_level)

    fmt = "%(asctime)s | %(levelname)s | %(name)s | %(message)s"
    datefmt = "%H:%M:%S"

    logging.basicConfig(level=level, format=fmt, datefmt=datefmt)

    logging.getLogger(name).debug(
        "logging initialized (level=%s, quiet=%s, basic_debug=%s)",
        logging.getLevelName(level),
        quiet,
        basic_debug,
    )
