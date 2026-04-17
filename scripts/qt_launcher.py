"""Compatibility shim.

The launcher has moved to the top-level :mod:`phigros_ui` package.  This
thin wrapper preserves the documented ``python3 scripts/qt_launcher.py``
invocation from README.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Ensure the repo root is on sys.path when this script is executed directly.
_ROOT = Path(__file__).resolve().parent.parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from phigros_ui.app import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
