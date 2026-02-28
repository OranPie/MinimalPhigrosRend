from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from ..math.util import clamp
from ..backends.pygame.utils.rendering import line_note_counts, track_seg_state


def print_recording_progress(
    t: float,
    record_frame_idx: int,
    record_start_time: float,
    record_end_time: Optional[float],
    chart_end: float,
):
    """Print recording progress to stdout."""
    try:
        end_for_prog = float(record_end_time) if record_end_time is not None else float(chart_end)
        denom = max(1e-6, float(end_for_prog) - float(record_start_time))
        ratio = clamp((float(t) - float(record_start_time)) / denom, 0.0, 1.0)
        msg = f"[record] {ratio*100:6.2f}%  frame={record_frame_idx:7d}  t={float(t):.3f}s"
        print("\r" + msg + " " * 8, end="", flush=True)
    except:
        pass


def print_recording_notes(
    t: float,
    note_times_by_line: Dict[int, List[float]],
    lines: List[Any],
    approach: float,
):
    """Print note count information during recording."""
    try:
        total_past = 0
        total_incoming = 0
        for lid in note_times_by_line:
            past, incoming = line_note_counts(note_times_by_line, int(lid), float(t), approach)
            total_past += int(past)
            total_incoming += int(incoming)
        seg_hint = ""
        if lines:
            try:
                ln0 = lines[0]
                seg_hint = f" seg(rot)={track_seg_state(ln0.rot)} seg(alpha)={track_seg_state(ln0.alpha)} seg(scroll)={track_seg_state(ln0.scroll_px)}"
            except:
                seg_hint = ""
        print(f"\n[record] past={int(total_past)} incoming={int(total_incoming)}{seg_hint}", flush=True)
    except:
        pass


def init_curses_ui() -> Tuple[bool, Any, Any, bool]:
    """Initialize curses UI for recording. Returns (cui_ok, cui, curses_mod, cui_has_color)."""
    try:
        import curses
        curses_mod = curses
        cui = curses.initscr()
        cui_has_color = False
        try:
            if curses.has_colors():
                curses.start_color()
                try:
                    curses.use_default_colors()
                except:
                    pass
                curses.init_pair(1, curses.COLOR_CYAN, -1)
                curses.init_pair(2, curses.COLOR_GREEN, -1)
                curses.init_pair(3, curses.COLOR_YELLOW, -1)
                curses.init_pair(4, curses.COLOR_RED, -1)
                curses.init_pair(5, curses.COLOR_MAGENTA, -1)
                curses.init_pair(6, curses.COLOR_WHITE, -1)
                cui_has_color = True
        except:
            cui_has_color = False
        curses.noecho()
        curses.cbreak()
        try:
            curses.curs_set(0)
        except:
            pass
        try:
            cui.nodelay(True)
        except:
            pass
        try:
            cui.keypad(True)
        except:
            pass
        return True, cui, curses_mod, cui_has_color
    except:
        return False, None, None, False


def cleanup_curses_ui(cui: Any):
    """Clean up curses UI."""
    if cui is None:
        return
    try:
        import curses
        curses.nocbreak()
        curses.echo()
        curses.endwin()
    except:
        pass


def handle_curses_input(cui: Any, curses_mod: Any, cui_view: int, cui_scroll: int, record_curses_fps: float) -> Tuple[bool, int, int, float]:
    """Handle curses keyboard input. Returns (should_quit, cui_view, cui_scroll, record_curses_fps)."""
    if cui is None or curses_mod is None:
        return False, cui_view, cui_scroll, record_curses_fps
    
    try:
        ch = cui.getch()
        if ch == ord('q') or ch == ord('Q'):
            return True, cui_view, cui_scroll, record_curses_fps
        elif ch == ord('h') or ch == ord('H'):
            cui_view = 0 if int(cui_view) != 0 else 1
            cui_scroll = 0
        elif ch == ord('j'):
            cui_scroll += 1
        elif ch == ord('k'):
            cui_scroll -= 1
        elif ch == ord('g'):
            cui_scroll = 0
        elif ch == ord('G'):
            cui_scroll = 10**9
        elif ch == ord('+') or ch == ord('='):
            record_curses_fps = min(60.0, float(record_curses_fps) + 1.0)
        elif ch == ord('-') or ch == ord('_'):
            record_curses_fps = max(1.0, float(record_curses_fps) - 1.0)
        else:
            try:
                if ch == int(curses_mod.KEY_UP):
                    cui_scroll -= 1
                elif ch == int(curses_mod.KEY_DOWN):
                    cui_scroll += 1
                elif ch == int(curses_mod.KEY_PPAGE):
                    cui_scroll -= 10
                elif ch == int(curses_mod.KEY_NPAGE):
                    cui_scroll += 10
                elif ch == int(curses_mod.KEY_HOME):
                    cui_scroll = 0
                elif ch == int(curses_mod.KEY_END):
                    cui_scroll = 10**9
            except:
                pass
    except:
        pass
    
    return False, cui_view, cui_scroll, record_curses_fps
