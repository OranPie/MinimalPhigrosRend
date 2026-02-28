from __future__ import annotations

from dataclasses import dataclass
from queue import Empty, SimpleQueue
from typing import Any, List, Optional, Tuple

try:
    from textual.app import App, ComposeResult
    from textual.containers import Horizontal, Vertical
    from textual.widgets import ProgressBar, RichLog, Static

    _TEXTUAL_OK = True
except Exception:  # pragma: no cover
    App = object  # type: ignore
    ComposeResult = object  # type: ignore
    Horizontal = object  # type: ignore
    Vertical = object  # type: ignore
    RichLog = object  # type: ignore
    Static = object  # type: ignore
    _TEXTUAL_OK = False


@dataclass
class RecordUISnapshot:
    header_lines: List[str]
    progress01: float
    incoming: List[str]
    past: List[str]
    line_props: List[str]
    notes: List[str]
    selected_line: int
    lines_total: int


@dataclass
class TextualUIState:
    should_quit: bool = False
    selected_line: int = 0


class TextualUIHandle:
    def __init__(self, *, state: TextualUIState, q: SimpleQueue, app: Any):
        self.state = state
        self.q = q
        self.app = app

    def push(self, snap: RecordUISnapshot) -> None:
        try:
            self.q.put(snap)
        except Exception:
            pass

    def stop(self) -> None:
        try:
            self.state.should_quit = True
        except Exception:
            pass
        try:
            if hasattr(self.app, "exit"):
                self.app.exit()
        except Exception:
            pass

    def run(self) -> None:
        """Run the Textual app. Must be called from the main thread."""
        try:
            self.app.run()
        except Exception:
            try:
                self.state.should_quit = True
            except Exception:
                pass


def init_textual_ui(*, refresh_hz: float = 10.0) -> Tuple[bool, Optional[TextualUIHandle], Optional[str]]:
    if not _TEXTUAL_OK:
        return False, None, "textual not installed"

    q: SimpleQueue = SimpleQueue()
    st = TextualUIState()

    class _RecordApp(App):
        CSS = """
        Screen { layout: vertical; }
        #header { height: auto; }
        #progress { height: 1; }
        #body { height: 1fr; }
        #left { width: 1fr; }
        #right { width: 1fr; }
        #incoming { height: 1fr; }
        #past { height: 1fr; }
        #line { height: 12; }
        #notes { height: 1fr; }
        """

        BINDINGS = [
            ("q", "quit", "Quit"),
            ("j", "down", "Line+"),
            ("k", "up", "Line-"),
            ("page_down", "page_down", "Line+10"),
            ("page_up", "page_up", "Line-10"),
            ("home", "home", "Top"),
            ("end", "end", "Bottom"),
        ]

        def __init__(self, *, state: TextualUIState, q: SimpleQueue, refresh_hz: float):
            super().__init__()
            self._state = state
            self._q = q
            self._refresh_hz = max(1.0, float(refresh_hz))
            self._incoming: RichLog
            self._past: RichLog
            self._line: Static
            self._notes: RichLog
            self._header: Static
            self._progress: ProgressBar

        def compose(self) -> ComposeResult:
            yield Static(id="header")
            yield ProgressBar(total=1000, id="progress")
            with Horizontal(id="body"):
                with Vertical(id="left"):
                    yield RichLog(id="incoming", wrap=False, highlight=False)
                    yield RichLog(id="past", wrap=False, highlight=False)
                with Vertical(id="right"):
                    yield Static(id="line")
                    yield RichLog(id="notes", wrap=False, highlight=False)

        def on_mount(self) -> None:
            self._header = self.query_one("#header", Static)
            self._progress = self.query_one("#progress", ProgressBar)
            self._incoming = self.query_one("#incoming", RichLog)
            self._past = self.query_one("#past", RichLog)
            self._line = self.query_one("#line", Static)
            self._notes = self.query_one("#notes", RichLog)
            self.set_interval(1.0 / self._refresh_hz, self._poll)

        def _poll(self) -> None:
            last: Optional[RecordUISnapshot] = None
            while True:
                try:
                    last = self._q.get_nowait()
                except Empty:
                    break
                except Exception:
                    break

            if last is None:
                if bool(getattr(self._state, "should_quit", False)):
                    try:
                        self.exit()
                    except Exception:
                        pass
                return

            try:
                self._header.update("\n".join(list(last.header_lines or [])))
            except Exception:
                pass

            try:
                p = float(last.progress01)
                if p < 0.0:
                    p = 0.0
                if p > 1.0:
                    p = 1.0
                self._progress.progress = int(round(p * 1000.0))
            except Exception:
                pass

            try:
                self._incoming.clear()
                self._incoming.write("incoming events")
                for s in last.incoming:
                    self._incoming.write(s)
            except Exception:
                pass

            try:
                self._past.clear()
                self._past.write("past events")
                for s in last.past:
                    self._past.write(s)
            except Exception:
                pass

            try:
                head = f"line {int(last.selected_line)+1}/{max(1, int(last.lines_total))}"
                body = "\n".join([head] + list(last.line_props))
                self._line.update(body)
            except Exception:
                pass

            try:
                self._notes.clear()
                self._notes.write("on-screen notes")
                for s in last.notes:
                    self._notes.write(s)
            except Exception:
                pass

            if bool(getattr(self._state, "should_quit", False)):
                try:
                    self.exit()
                except Exception:
                    pass

        def action_quit(self) -> None:
            try:
                self._state.should_quit = True
            except Exception:
                pass
            try:
                self.exit()
            except Exception:
                pass

        def _bump(self, d: int) -> None:
            try:
                self._state.selected_line = max(0, int(self._state.selected_line) + int(d))
            except Exception:
                pass

        def action_down(self) -> None:
            self._bump(+1)

        def action_up(self) -> None:
            self._bump(-1)

        def action_page_down(self) -> None:
            self._bump(+10)

        def action_page_up(self) -> None:
            self._bump(-10)

        def action_home(self) -> None:
            try:
                self._state.selected_line = 0
            except Exception:
                pass

        def action_end(self) -> None:
            try:
                self._state.selected_line = 10**9
            except Exception:
                pass

    app = _RecordApp(state=st, q=q, refresh_hz=float(refresh_hz))

    return True, TextualUIHandle(state=st, q=q, app=app), None
