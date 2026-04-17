"""Reusable Qt primitives for phigros_ui tabs."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QWidget,
)

from .common import INTERNAL_ASSET_LABEL


class FileRow(QWidget):
    """Label + QLineEdit + Browse button.

    The Browse button opens a file or directory picker depending on
    ``directory``.  Shared across build/renderer/config tabs.
    """

    def __init__(
        self,
        title: str,
        default: str = "",
        *,
        directory: bool = False,
        tooltip: str = "",
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._directory = directory
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.label = QLabel(title)
        self.edit = QLineEdit(default)
        self.button = QPushButton("Browse")
        layout.addWidget(self.label)
        layout.addWidget(self.edit, 1)
        layout.addWidget(self.button)
        self.button.clicked.connect(self.browse)
        if tooltip:
            self.setToolTip(tooltip)
            self.label.setToolTip(tooltip)
            self.edit.setToolTip(tooltip)
            self.button.setToolTip(tooltip)

    def browse(self) -> None:
        current_text = self.edit.text().strip()
        current = (
            str(Path.cwd())
            if current_text == INTERNAL_ASSET_LABEL
            else (current_text or str(Path.cwd()))
        )
        if self._directory:
            selected = QFileDialog.getExistingDirectory(self, "Select Directory", current)
        else:
            selected, _ = QFileDialog.getOpenFileName(self, "Select File", current)
        if selected:
            self.edit.setText(selected)

    def text(self) -> str:
        return self.edit.text().strip()

    def setText(self, value: str) -> None:
        self.edit.setText(value)


def add_form_row(
    form: QFormLayout,
    label_text: str,
    widget: QWidget,
    tooltip: str,
) -> QLabel:
    label = QLabel(label_text)
    label.setToolTip(tooltip)
    widget.setToolTip(tooltip)
    form.addRow(label, widget)
    return label
