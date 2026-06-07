"""Simple config editor tab for ``config/config.jsonc``.

This is deliberately minimal — a monospace editor with open/save/reload
buttons.  It does *not* validate JSONC; the renderer's parser already emits
line/column diagnostics on misuse.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Signal
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..common import DEFAULT_CONFIG


class ConfigEditorTab(QWidget):
    use_in_renderer = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        root = QVBoxLayout(self)

        header = QHBoxLayout()
        self.path_field = QLineEdit(str(DEFAULT_CONFIG))
        self.path_field.setPlaceholderText("Path to JSONC config")
        header.addWidget(QLabel("Path"))
        header.addWidget(self.path_field, 1)
        self.browse_btn = QPushButton("Browse…")
        self.open_btn = QPushButton("Open")
        self.save_btn = QPushButton("Save")
        self.reload_btn = QPushButton("Reload")
        self.use_btn = QPushButton("Use in Renderer")
        self.use_btn.setToolTip("Set the Renderer tab's config path to this file.")
        for btn in (self.browse_btn, self.open_btn, self.save_btn, self.reload_btn, self.use_btn):
            header.addWidget(btn)
        root.addLayout(header)

        self.editor = QPlainTextEdit()
        mono = QFont("Menlo")
        mono.setStyleHint(QFont.Monospace)
        self.editor.setFont(mono)
        self.editor.setLineWrapMode(QPlainTextEdit.NoWrap)
        root.addWidget(self.editor, 1)

        self.status = QLabel("")
        self.status.setWordWrap(True)
        root.addWidget(self.status)

        self.browse_btn.clicked.connect(self._browse)
        self.open_btn.clicked.connect(self._open)
        self.save_btn.clicked.connect(self._save)
        self.reload_btn.clicked.connect(self._open)
        self.use_btn.clicked.connect(self._use_in_renderer)

        if DEFAULT_CONFIG.is_file():
            self._open()

    def _browse(self) -> None:
        selected, _ = QFileDialog.getOpenFileName(
            self, "Select JSONC config", str(DEFAULT_CONFIG.parent),
            "JSONC/JSON (*.jsonc *.json);;All files (*)",
        )
        if selected:
            self.path_field.setText(selected)

    def _open(self) -> None:
        path = Path(self.path_field.text().strip())
        if not path.is_file():
            self.status.setText(f"Cannot open: {path}")
            return
        try:
            self.editor.setPlainText(path.read_text(encoding="utf-8"))
            self.status.setText(f"Loaded {path} ({path.stat().st_size} bytes)")
        except Exception as exc:
            QMessageBox.critical(self, "Open Config", str(exc))

    def _save(self) -> None:
        path = Path(self.path_field.text().strip())
        if not str(path):
            return
        try:
            path.write_text(self.editor.toPlainText(), encoding="utf-8")
            self.status.setText(f"Saved {path}")
        except Exception as exc:
            QMessageBox.critical(self, "Save Config", str(exc))

    def _use_in_renderer(self) -> None:
        path = self.path_field.text().strip()
        if path:
            self.use_in_renderer.emit(path)
