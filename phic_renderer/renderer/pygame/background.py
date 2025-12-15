from __future__ import annotations

from typing import Optional, Tuple

import pygame


def load_background(bg_file: Optional[str], W: int, H: int, blur_factor: int) -> Tuple[Optional[pygame.Surface], Optional[pygame.Surface]]:
    if not bg_file:
        return None, None

    bg_base = pygame.image.load(str(bg_file)).convert()
    bg_base = pygame.transform.smoothscale(bg_base, (W, H))

    factor = max(1, int(blur_factor))
    w, h = bg_base.get_size()
    small_surf = pygame.transform.smoothscale(bg_base, (max(1, w // factor), max(1, h // factor)))
    bg_blurred = pygame.transform.smoothscale(small_surf, (w, h))
    return bg_base, bg_blurred
