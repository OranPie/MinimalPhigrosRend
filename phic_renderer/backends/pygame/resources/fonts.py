from __future__ import annotations

import os
from typing import Optional, Tuple

import pygame


def load_fonts(font_path: Optional[str], font_size_multiplier: float) -> Tuple[pygame.font.Font, pygame.font.Font]:
    font_mul = float(font_size_multiplier) if font_size_multiplier is not None else 1.0
    if font_mul <= 1e-9:
        font_mul = 1.0
    font_size = max(1, int(round(22 * font_mul)))
    small_size = max(1, int(round(16 * font_mul)))

    font = None
    small = None

    if font_path and os.path.exists(str(font_path)):
        try:
            font = pygame.font.Font(str(font_path), font_size)
            small = pygame.font.Font(str(font_path), small_size)
        except:
            font = None
            small = None

    if font is None or small is None:
        if os.path.exists("cmdysj.ttf"):
            try:
                font = pygame.font.Font("cmdysj.ttf", font_size)
                small = pygame.font.Font("cmdysj.ttf", small_size)
            except:
                font = None
                small = None

    if font is None or small is None:
        font = pygame.font.SysFont("consolas", font_size)
        small = pygame.font.SysFont("consolas", small_size)

    return font, small
