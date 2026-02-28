from __future__ import annotations

import pygame


def draw_poly_rgba(dst: pygame.Surface, pts, rgba):
    pygame.draw.polygon(dst, rgba, pts)


def draw_poly_outline_rgba(dst: pygame.Surface, pts, rgba, width=2):
    pygame.draw.polygon(dst, rgba, pts, width)


def draw_line_rgba(dst: pygame.Surface, p0, p1, rgba, width=3):
    pygame.draw.line(dst, rgba, p0, p1, width)


def draw_ring(dst: pygame.Surface, x: float, y: float, r: int, rgba, thickness=3):
    if r <= 0 or rgba[3] <= 0:
        return
    pygame.draw.circle(dst, rgba, (int(x), int(y)), r, thickness)
