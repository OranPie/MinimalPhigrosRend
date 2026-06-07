"""Shared log viewer tab.

Every tab that spawns a subprocess emits its output on an ``output`` signal;
:class:`LogTab` accumulates those chunks into a monospace QPlainTextEdit with
optional word-wrap, a search bar, copy-all, and save-to-file.
"""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QTextCursor, QTextDocument
from PySide6.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
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
        self.copy_all_btn = QPushButton("Copy All")
        self.copy_all_btn.clicked.connect(self._copy_all)
        header.addWidget(self.copy_all_btn)
        self.save_btn = QPushButton("Save…")
        self.save_btn.clicked.connect(self._save_to_file)
        header.addWidget(self.save_btn)
        self.clear_btn = QPushButton("Clear")
        self.clear_btn.clicked.connect(self._clear)
        header.addWidget(self.clear_btn)
        root.addLayout(header)

        search_row = QHBoxLayout()
        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("Search log…")
        self.search_edit.returnPressed.connect(self._find_next)
        self.find_prev_btn = QPushButton("◀ Prev")
        self.find_prev_btn.clicked.connect(self._find_prev)
        self.find_next_btn = QPushButton("Next ▶")
        self.find_next_btn.clicked.connect(self._find_next)
        search_row.addWidget(self.search_edit, 1)
        search_row.addWidget(self.find_prev_btn)
        search_row.addWidget(self.find_next_btn)
        root.addLayout(search_row)

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

    def _copy_all(self) -> None:
        from PySide6.QtWidgets import QApplication
        QApplication.clipboard().setText(self.view.toPlainText())

    def _save_to_file(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Save Log", "phigros_ui.log", "Text files (*.log *.txt);;All files (*)"
        )
        if path:
            try:
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(self.view.toPlainText())
            except Exception as exc:
                from PySide6.QtWidgets import QMessageBox
                QMessageBox.critical(self, "Save Log", str(exc))

    def _find_next(self) -> None:
        needle = self.search_edit.text()
        if not needle:
            return
        found = self.view.find(needle)
        if not found:
            # Wrap around from beginning
            self.view.moveCursor(QTextCursor.MoveOperation.Start)
            self.view.find(needle)

    def _find_prev(self) -> None:
        needle = self.search_edit.text()
        if not needle:
            return
        found = self.view.find(needle, QTextDocument.FindFlag.FindBackward)
        if not found:
            # Wrap around from end
            self.view.moveCursor(QTextCursor.MoveOperation.End)
            self.view.find(needle, QTextDocument.FindFlag.FindBackward)
