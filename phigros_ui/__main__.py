from __future__ import annotations

import argparse
import sys


def _parse_args() -> dict:
    parser = argparse.ArgumentParser(
        prog="python -m phigros_ui",
        description="MinimalPhigrosRend UI — PySide6 launcher for phigros_render.",
    )
    parser.add_argument(
        "chart",
        nargs="?",
        metavar="CHART",
        help="Pre-fill the chart path in the Renderer tab.",
    )
    parser.add_argument(
        "--preset",
        metavar="NAME",
        help="Load a named preset from .phigros_ui/presets/<NAME>.json on startup.",
    )
    parser.add_argument(
        "--tab",
        metavar="NAME",
        choices=["renderer", "build", "config", "log"],
        help="Open this tab on startup (renderer/build/config/log).",
    )
    parser.add_argument(
        "--charts-dir",
        metavar="DIR",
        help="Override the charts directory scanned for chart entries.",
    )
    parser.add_argument(
        "--config",
        metavar="FILE",
        help="Pre-fill the renderer config path (also loads it in the Config tab).",
    )
    # Strip Qt-specific arguments (those starting with a single dash followed by a
    # platform/plugin flag) to prevent argparse conflicts on some platforms.
    argv = [a for a in sys.argv[1:] if not (a.startswith("-style") or a.startswith("-platform"))]
    args = parser.parse_args(argv)
    return {
        "chart": args.chart,
        "preset": args.preset,
        "tab": args.tab,
        "charts_dir": args.charts_dir,
        "config": args.config,
    }


def main() -> int:
    startup = _parse_args()
    from .app import main as _main
    return _main(startup=startup)


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
