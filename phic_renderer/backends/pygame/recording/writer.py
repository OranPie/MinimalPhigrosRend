from __future__ import annotations

from typing import Any, Optional

import pygame


def write_record_frame(
    *,
    recorder: Any,
    display_frame: pygame.Surface,
    record_use_curses: bool,
    cui_ok: bool,
    cui: Any,
) -> Optional[Exception]:
    """Write a frame to the recorder.

    Returns an Exception instance on failure, otherwise None.
    """
    if not recorder:
        return None

    try:
        # Fast path: avoid numpy conversion+transpose (very expensive)
        if hasattr(recorder, "write_frame_bytes"):
            frame_bytes = pygame.image.tostring(display_frame, "RGB")
            recorder.write_frame_bytes(frame_bytes)
            return None

        # Fallback: Convert pygame surface to array3d and transpose to (H, W, 3).
        frame_array = pygame.surfarray.array3d(display_frame)
        try:
            frame_array = frame_array.transpose(1, 0, 2)
        except Exception:
            # As a very safe fallback, keep original (may be wrong orientation but avoids hard crash)
            pass
        recorder.write_frame(frame_array)
        return None
    except Exception as e:
        if (not record_use_curses) or (not cui_ok) or (cui is None):
            try:
                print(f"\r[Recording] Error writing frame: {e}", flush=True)
            except Exception:
                pass
        return e


def save_record_png(
    *,
    display_frame: pygame.Surface,
    record_dir: Any,
    record_frame_idx: int,
    record_use_curses: bool,
    cui_ok: bool,
    cui: Any,
) -> Optional[Exception]:
    """Fallback to saving frames as PNG files."""
    if not record_dir:
        return None

    try:
        import os

        os.makedirs(str(record_dir), exist_ok=True)
        out_p = os.path.join(str(record_dir), f"frame_{int(record_frame_idx):06d}.png")
        pygame.image.save(display_frame, out_p)
        return None
    except Exception as e:
        if (not record_use_curses) or (not cui_ok) or (cui is None):
            try:
                print(f"\r[Recording] Error saving PNG: {e}", flush=True)
            except Exception:
                pass
        return e
