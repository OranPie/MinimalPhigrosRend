"""Main application window for MinimalPhigrosRend.

Wires together the build, renderer, log and config-editor tabs, adds a menu
bar with File/View/Help actions and a status bar showing whether a child
process is currently running.  Window geometry, splitter positions and
active tab are persisted via :class:`QSettings` so subsequent launches
restore the previous state.
"""

from __future__ import annotations

import sys

from PySide6.QtCore import QSettings, Qt
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import (
    QApplication,
    QLabel,
    QMainWindow,
    QMessageBox,
    QTabWidget,
)

from . import __version__
from .common import ROOT_DIR, clipboard_copy
from .tabs import BuildTab, ConfigEditorTab, LogTab, RendererTab


class LauncherWindow(QMainWindow):
    """Top-level window for the phigros_ui app."""

    ORG = "MinimalPhigrosRend"
    APP = "phigros_ui"

    def __init__(self, startup: dict | None = None) -> None:
        super().__init__()
        self.setWindowTitle(f"MinimalPhigrosRend — UI {__version__}")
        self.resize(1320, 900)

        self._settings = QSettings(self.ORG, self.APP)

        # ---- tabs -------------------------------------------------------- #
        self.tabs = QTabWidget()
        self.build_tab = BuildTab()
        self.renderer_tab = RendererTab()
        self.log_tab = LogTab()
        self.config_tab = ConfigEditorTab()

        self.tabs.addTab(self.renderer_tab, "Renderer")
        self.tabs.addTab(self.build_tab, "Build")
        self.tabs.addTab(self.config_tab, "Config")
        self.tabs.addTab(self.log_tab, "Log")
        self.setCentralWidget(self.tabs)

        # Route all subprocess output through the shared log viewer.
        self.build_tab.output.connect(self._on_log_output)
        self.renderer_tab.output.connect(self._on_log_output)

        # Wire config-editor "Use in Renderer" signal.
        self.config_tab.use_in_renderer.connect(
            lambda p: self.renderer_tab.config.setText(p)
        )

        # ---- menu & status bar ------------------------------------------ #
        self._build_menu()
        self.status_label = QLabel("Ready")
        self.statusBar().addWidget(self.status_label, 1)
        self._running_processes = 0

        # Track running state for the status bar.
        self.build_tab._runner.started.connect(self._on_process_started)
        self.build_tab._runner.sequence_finished.connect(self._on_process_finished)
        self.renderer_tab._runner.started.connect(self._on_process_started)
        self.renderer_tab._runner.sequence_finished.connect(self._on_process_finished)

        self._restore_state()
        self._apply_startup(startup or {})

    # ------------------------------------------------------------------ #
    # startup args                                                       #
    # ------------------------------------------------------------------ #

    def _apply_startup(self, startup: dict) -> None:
        """Apply CLI startup overrides after the window is fully built."""
        if not startup:
            return
        rt = self.renderer_tab

        if startup.get("chart"):
            rt.chart.setText(startup["chart"])
        if startup.get("charts_dir"):
            rt._charts_dir_edit.setText(startup["charts_dir"])
        if startup.get("config"):
            rt.config.setText(startup["config"])
            # also update the config editor
            self.config_tab.path_field.setText(startup["config"])
            self.config_tab._open()
        if startup.get("preset"):
            from .presets import load_preset
            try:
                opts = load_preset(startup["preset"])
                rt.apply_options(opts)
            except Exception as exc:
                from PySide6.QtWidgets import QMessageBox
                QMessageBox.warning(self, "CLI --preset", str(exc))
        tab_name = startup.get("tab", "").lower()
        tab_map = {"renderer": 0, "build": 1, "config": 2, "log": 3}
        if tab_name in tab_map:
            self.tabs.setCurrentIndex(tab_map[tab_name])

    # ------------------------------------------------------------------ #
    # menu                                                               #
    # ------------------------------------------------------------------ #

    def _build_menu(self) -> None:
        bar = self.menuBar()

        file_menu = bar.addMenu("&File")
        act_copy = QAction("Copy Renderer &Command", self)
        act_copy.setShortcut(QKeySequence("Ctrl+Shift+C"))
        act_copy.triggered.connect(lambda: clipboard_copy(self.renderer_tab.preview.toPlainText()))
        file_menu.addAction(act_copy)

        act_launch = QAction("&Launch Renderer", self)
        act_launch.setShortcut(QKeySequence("Ctrl+R"))
        act_launch.triggered.connect(self.renderer_tab.launch_renderer)
        file_menu.addAction(act_launch)

        act_build = QAction("Run &Build", self)
        act_build.setShortcut(QKeySequence("Ctrl+B"))
        act_build.triggered.connect(self.build_tab.run_build)
        file_menu.addAction(act_build)

        file_menu.addSeparator()
        act_quit = QAction("&Quit", self)
        act_quit.setShortcut(QKeySequence.Quit)
        act_quit.triggered.connect(self.close)
        file_menu.addAction(act_quit)

        view_menu = bar.addMenu("&View")
        for idx, name in enumerate(("Renderer", "Build", "Config", "Log")):
            act = QAction(f"Go to &{name}", self)
            act.setShortcut(QKeySequence(f"Ctrl+{idx + 1}"))
            act.triggered.connect(lambda _checked=False, i=idx: self.tabs.setCurrentIndex(i))
            view_menu.addAction(act)

        help_menu = bar.addMenu("&Help")
        act_about = QAction("&About", self)
        act_about.triggered.connect(self._show_about)
        help_menu.addAction(act_about)

    def _show_about(self) -> None:
        QMessageBox.information(
            self,
            "About phigros_ui",
            f"MinimalPhigrosRend UI\nVersion {__version__}\n\n"
            f"Repository root: {ROOT_DIR}\n"
            "Unified PySide6 front-end for the renderer, build system, and chart tooling.",
        )

    # ------------------------------------------------------------------ #
    # status bar                                                         #
    # ------------------------------------------------------------------ #

    def _on_log_output(self, text: str) -> None:
        self.log_tab.append(text)

    def _on_process_started(self, _command: list) -> None:
        self._running_processes += 1
        self._refresh_status()

    def _on_process_finished(self, _code: int) -> None:
        self._running_processes = max(0, self._running_processes - 1)
        self._refresh_status()

    def _refresh_status(self) -> None:
        if self._running_processes > 0:
            self.status_label.setText(f"Running… ({self._running_processes} process)")
        else:
            self.status_label.setText("Ready")

    # ------------------------------------------------------------------ #
    # persistence                                                        #
    # ------------------------------------------------------------------ #

    def _restore_state(self) -> None:
        geom = self._settings.value("geometry")
        if geom is not None:
            self.restoreGeometry(geom)
        state = self._settings.value("windowState")
        if state is not None:
            self.restoreState(state)
        last_tab = self._settings.value("lastTab", 0, type=int)
        if 0 <= int(last_tab) < self.tabs.count():
            self.tabs.setCurrentIndex(int(last_tab))

    def closeEvent(self, event) -> None:  # noqa: N802 (Qt override)
        self._settings.setValue("geometry", self.saveGeometry())
        self._settings.setValue("windowState", self.saveState())
        self._settings.setValue("lastTab", self.tabs.currentIndex())
        super().closeEvent(event)


def main(startup: dict | None = None) -> int:
    # PySide6 import delayed to keep ``python -m phigros_ui --help``
    # (or plain ``import phigros_ui``) cheap.
    try:
        from PySide6.QtWidgets import QApplication as _QApp  # noqa: F401
    except ImportError:  # pragma: no cover
        sys.exit("PySide6 is required. Install it with: python3 -m pip install PySide6")

    app = QApplication.instance() or QApplication(sys.argv)
    app.setApplicationName(LauncherWindow.APP)
    app.setOrganizationName(LauncherWindow.ORG)
    window = LauncherWindow(startup=startup)
    window.show()
    return app.exec()


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
