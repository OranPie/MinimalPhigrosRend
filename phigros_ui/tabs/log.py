"""Shared log viewer tab.

Every tab that spawns a subprocess emits its output on an ``output`` signal;
:class:`LogTab` accumulates those chunks into a monospace QPlainTextEdit with
optional word-wrap and a clear button.
"""

from __future__ import annotations

from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


class LogTab(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        root = QVBoxLayout(self)

        header = QHBoxLayout()
        header.addWidget(QLabel("Merged stdout/stderr from builds and renderer runs."))
        header.addStretch(1)
        self.wrap = QCheckBox("Word wrap")
        self.wrap.toggled.connect(self._toggle_wrap)
        header.addWidget(self.wrap)
        self.clear_btn = QPushButton("Clear")
        self.clear_btn.clicked.connect(self._clear)
        header.addWidget(self.clear_btn)
        root.addLayout(header)

        self.view = QPlainTextEdit()
        self.view.setReadOnly(True)
        self.view.setMaximumBlockCount(10000)
        mono = QFont("Menlo")
        mono.setStyleHint(QFont.Monospace)
        self.view.setFont(mono)
        self.view.setLineWrapMode(QPlainTextEdit.NoWrap)
        root.addWidget(self.view, 1)

    def append(self, text: str) -> None:
        if not text:
            return
        # insertPlainText preserves an in-progress line, while appendPlainText
        # would force a newline.  We want renderer output to look like a
        # terminal, so use insertPlainText and autoscroll.
        cursor = self.view.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        cursor.insertText(text)
        self.view.setTextCursor(cursor)
        self.view.ensureCursorVisible()

    def _toggle_wrap(self, on: bool) -> None:
        self.view.setLineWrapMode(QPlainTextEdit.WidgetWidth if on else QPlainTextEdit.NoWrap)

    def _clear(self) -> None:
        self.view.clear()
