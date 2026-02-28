from __future__ import annotations

from typing import Any, Dict, Optional

import pygame

from ....ui.transitions import TransitionPhase


def render_transition_overlay(
    screen: pygame.Surface,
    *,
    font: pygame.font.Font,
    small: pygame.font.Font,
    W: int,
    H: int,
    phase: TransitionPhase,
    progress: float,
    chart_info: Optional[Dict[str, Any]] = None,
    judge: Any = None,
    total_notes: Optional[int] = None,
) -> None:
    p = float(progress)
    if p < 0.0:
        p = 0.0
    if p > 1.0:
        p = 1.0

    if phase == TransitionPhase.GAMEPLAY:
        return

    overlay = pygame.Surface((int(W), int(H)), pygame.SRCALPHA)

    if phase in (TransitionPhase.INTRO_LOADING, TransitionPhase.INTRO_LINE_OPEN):
        alpha = int(200)
    elif phase in (TransitionPhase.OUTRO_SETTLEMENT, TransitionPhase.OUTRO_LINE_CLOSE):
        alpha = int(220)
    else:
        alpha = int(240)

    overlay.fill((0, 0, 0, int(alpha)))

    title = ""
    subtitle = ""
    if phase == TransitionPhase.INTRO_LOADING:
        title = "LOADING"
        subtitle = "Preparing resources"
    elif phase == TransitionPhase.INTRO_LINE_OPEN:
        title = "READY"
        subtitle = "Get set"
    elif phase == TransitionPhase.OUTRO_SETTLEMENT:
        title = "RESULT"
        if judge is not None and total_notes is not None:
            try:
                combo = int(getattr(judge, "max_combo", 0) or 0)
                acc = float(getattr(judge, "acc", 0.0) or 0.0)
                subtitle = f"MAX COMBO {combo}/{int(total_notes)}   ACC {acc*100:6.2f}%"
            except Exception:
                subtitle = ""
    elif phase == TransitionPhase.OUTRO_LINE_CLOSE:
        title = "FINISH"
        subtitle = "Thanks for playing"
    else:
        title = ""
        subtitle = ""

    cx = int(W // 2)
    cy = int(H // 2)

    if title:
        t1 = font.render(title, True, (240, 240, 240))
        overlay.blit(t1, (cx - t1.get_width() // 2, cy - t1.get_height() - 10))

    if subtitle:
        t2 = small.render(subtitle, True, (200, 200, 200))
        overlay.blit(t2, (cx - t2.get_width() // 2, cy + 6))

    bar_w = int(min(float(W) * 0.55, float(W) - 80))
    bar_h = max(6, int(round(float(small.get_linesize()) * 0.35)))
    x0 = cx - bar_w // 2
    y0 = cy + int(round(float(small.get_linesize()) * 2.0))

    pygame.draw.rect(overlay, (70, 70, 70, 220), pygame.Rect(x0, y0, bar_w, bar_h), border_radius=3)
    pygame.draw.rect(overlay, (235, 235, 235, 235), pygame.Rect(x0, y0, int(round(bar_w * p)), bar_h), border_radius=3)

    if chart_info and phase in (TransitionPhase.INTRO_LOADING, TransitionPhase.INTRO_LINE_OPEN):
        try:
            name = str((chart_info or {}).get("name") or "")
        except Exception:
            name = ""
        if name:
            t3 = small.render(name, True, (180, 180, 180))
            overlay.blit(t3, (cx - t3.get_width() // 2, y0 + bar_h + 12))

    screen.blit(overlay, (0, 0))
