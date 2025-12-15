from __future__ import annotations

from typing import Any


def run_loop(*, pygame: Any, clock: Any, screen: Any, app: Any) -> None:
    running = True
    holding_input = False
    key_down = False
    prev_down = False
    while running:
        dt = clock.tick(120) / 1000.0
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                running = False
            elif ev.type == pygame.KEYDOWN and ev.key == pygame.K_ESCAPE:
                running = False
            elif ev.type == pygame.KEYDOWN and ev.key == pygame.K_SPACE:
                key_down = True
            elif ev.type == pygame.KEYUP and ev.key == pygame.K_SPACE:
                key_down = False
            elif ev.type == pygame.MOUSEBUTTONDOWN and getattr(ev, "button", None) == 1:
                holding_input = True
            elif ev.type == pygame.MOUSEBUTTONUP and getattr(ev, "button", None) == 1:
                holding_input = False

        down = bool(holding_input or key_down)
        press_edge = bool(down and (not prev_down))
        prev_down = down
        try:
            if hasattr(app, "set_input"):
                app.set_input(down=down, press_edge=press_edge)
        except:
            pass

        app.render(dt)
        pygame.display.flip()
