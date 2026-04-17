"""JSON-backed preset save/load for :class:`RendererOptions`.

Presets live in ``<repo>/.phigros_ui/presets/<name>.json``.  The directory is
created lazily and ignored by git if the repo's ``.gitignore`` covers it (we
recommend adding ``.phigros_ui/`` there, but do not modify it automatically).
"""

from __future__ import annotations

import json
from pathlib import Path

from .common import ROOT_DIR, RendererOptions

PRESET_DIR = ROOT_DIR / ".phigros_ui" / "presets"


def _ensure_dir() -> Path:
    PRESET_DIR.mkdir(parents=True, exist_ok=True)
    return PRESET_DIR


def list_presets() -> list[str]:
    if not PRESET_DIR.is_dir():
        return []
    return sorted(p.stem for p in PRESET_DIR.glob("*.json"))


def save_preset(name: str, options: RendererOptions) -> Path:
    if not name.strip():
        raise ValueError("Preset name may not be empty.")
    safe = "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in name.strip())
    path = _ensure_dir() / f"{safe}.json"
    path.write_text(json.dumps(options.to_dict(), indent=2, sort_keys=True), encoding="utf-8")
    return path


def load_preset(name: str) -> RendererOptions:
    path = PRESET_DIR / f"{name}.json"
    if not path.is_file():
        raise FileNotFoundError(f"Preset not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    return RendererOptions.from_dict(data)


def delete_preset(name: str) -> bool:
    path = PRESET_DIR / f"{name}.json"
    if path.is_file():
        path.unlink()
        return True
    return False
