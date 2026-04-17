"""Build tab — compose CMake configure/build commands with a live preview
and non-blocking execution via :class:`~phigros_ui.process.ProcessRunner`."""

from __future__ import annotations

import shlex

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ..common import (
    BUILD_PROFILES,
    BuildRequest,
    build_commands,
    clipboard_copy,
    cpu_jobs,
    format_command,
)
from ..process import ProcessRunner
from ..widgets import add_form_row


class BuildTab(QWidget):
    #: Emitted with every chunk of output from the running build command.  The
    #: main window wires this into the shared log tab.
    output = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._runner = ProcessRunner(self)
        self._runner.output.connect(self.output.emit)
        self._runner.started.connect(lambda cmd: self.output.emit(f"\n$ {format_command(cmd)}\n"))
        self._runner.sequence_finished.connect(self._on_sequence_finished)

        layout = QVBoxLayout(self)

        form_group = QGroupBox("Build Profile")
        form = QFormLayout(form_group)
        self.profile = QComboBox()
        self.profile.addItems(BUILD_PROFILES.keys())
        self.build_type = QComboBox()
        self.build_type.addItems(["Release", "Debug", "RelWithDebInfo"])
        self.jobs = QSpinBox()
        self.jobs.setRange(1, 256)
        self.jobs.setValue(cpu_jobs())
        self.target = QLineEdit()
        self.android_abi = QComboBox()
        self.android_abi.addItems(["arm64-v8a", "armeabi-v7a", "x86", "x86_64"])
        self.android_api = QSpinBox()
        self.android_api.setRange(21, 40)
        self.android_api.setValue(24)
        self.extra_args = QLineEdit()

        add_form_row(form, "Profile", self.profile, "Build family exposed by the repository: desktop, python, web, android, ios, or tests.")
        add_form_row(form, "Build type", self.build_type, "CMake build type. Use Debug for development, Release for distribution, RelWithDebInfo for optimized builds with symbols.")
        add_form_row(form, "Parallel jobs", self.jobs, "Worker count passed to `cmake --build --parallel`.")
        add_form_row(form, "Target override", self.target, "Optional CMake target. Leave empty for the profile default such as `phigros_render`, `_core`, or `run-tests`.")
        add_form_row(form, "Android ABI", self.android_abi, "Valid ABI choices from the Android helper flow: armeabi-v7a, arm64-v8a, x86, x86_64.")
        add_form_row(form, "Android API", self.android_api, "Android platform level used for the NDK toolchain, default 24.")
        add_form_row(form, "Extra CMake args", self.extra_args, "Extra configure-time arguments appended verbatim to the CMake configure command.")
        layout.addWidget(form_group)

        flags_group = QGroupBox("Build Flags")
        flags_layout = QGridLayout(flags_group)
        self.clean = QCheckBox("Clean build directory")
        self.use_bgfx = QCheckBox("Enable bgfx")
        self.use_sdl3 = QCheckBox("Use SDL3")
        self.use_sdl3.setChecked(True)
        self.use_libav = QCheckBox("Use libav")
        self.use_libav.setChecked(True)
        self.use_lzma = QCheckBox("Use LZMA")
        self.use_encryption = QCheckBox("Use encryption")
        self.use_encryption.setChecked(True)
        self.use_sanitizers = QCheckBox("Use sanitizers")
        self.run_tests = QCheckBox("Run tests after build")
        self.clean.setToolTip("Delete the selected build directory before configuring.")
        self.use_bgfx.setToolTip("Maps to `-DUSE_BGFX=ON`. Desktop renderer only.")
        self.use_sdl3.setToolTip("Maps to `-DUSE_SDL3=ON`. Desktop helper defaults to SDL3.")
        self.use_libav.setToolTip("Maps to `-DUSE_LIBAV=ON`. Enables libav integration where supported.")
        self.use_lzma.setToolTip("Maps to `-DUSE_LZMA=ON`. Optional PHBC compression support.")
        self.use_encryption.setToolTip("Maps to `-DUSE_ENCRYPTION=ON`. Optional OpenSSL-backed PHBC encryption support.")
        self.use_sanitizers.setToolTip("Maps to `-DUSE_SANITIZERS=ON`. Intended for development builds.")
        self.run_tests.setToolTip("After building a non-`run-tests` target, run discovered native test binaries.")
        checkboxes = [
            self.clean, self.use_bgfx, self.use_sdl3, self.use_libav, self.use_lzma,
            self.use_encryption, self.use_sanitizers, self.run_tests,
        ]
        for idx, checkbox in enumerate(checkboxes):
            flags_layout.addWidget(checkbox, idx // 2, idx % 2)
        layout.addWidget(flags_group)

        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        self.summary = QLabel()
        self.summary.setWordWrap(True)
        layout.addWidget(QLabel("Build command preview"))
        layout.addWidget(self.summary)
        layout.addWidget(self.preview, 1)

        buttons = QHBoxLayout()
        self.copy_button = QPushButton("Copy Commands")
        self.run_button = QPushButton("Run Build")
        self.abort_button = QPushButton("Abort")
        self.abort_button.setEnabled(False)
        buttons.addWidget(self.copy_button)
        buttons.addWidget(self.run_button)
        buttons.addWidget(self.abort_button)
        layout.addLayout(buttons)

        self.profile.currentTextChanged.connect(self.refresh_preview)
        self.build_type.currentTextChanged.connect(self.refresh_preview)
        self.jobs.valueChanged.connect(self.refresh_preview)
        self.target.textChanged.connect(self.refresh_preview)
        self.android_abi.currentTextChanged.connect(self.refresh_preview)
        self.android_api.valueChanged.connect(self.refresh_preview)
        self.extra_args.textChanged.connect(self.refresh_preview)
        for checkbox in checkboxes:
            checkbox.toggled.connect(self.refresh_preview)
        self.copy_button.clicked.connect(self.copy_preview)
        self.run_button.clicked.connect(self.run_build)
        self.abort_button.clicked.connect(self._runner.abort)
        self.refresh_preview()

    # ------------------------------------------------------------------ #
    # model                                                              #
    # ------------------------------------------------------------------ #

    def request(self) -> BuildRequest:
        target = self.target.text().strip() or None
        extra = shlex.split(self.extra_args.text().strip()) if self.extra_args.text().strip() else None
        return BuildRequest(
            profile=self.profile.currentText(),
            build_type=self.build_type.currentText(),
            jobs=self.jobs.value(),
            clean=self.clean.isChecked(),
            build_target=target,
            use_bgfx=self.use_bgfx.isChecked(),
            use_sdl3=self.use_sdl3.isChecked(),
            use_libav=self.use_libav.isChecked(),
            use_lzma=self.use_lzma.isChecked(),
            use_encryption=self.use_encryption.isChecked(),
            use_sanitizers=self.use_sanitizers.isChecked(),
            android_abi=self.android_abi.currentText(),
            android_api=self.android_api.value(),
            run_tests=self.run_tests.isChecked(),
            extra_cmake_args=extra,
        )

    def refresh_preview(self) -> None:
        try:
            commands = build_commands(self.request())
            request = self.request()
            self.summary.setText(
                f"Profile `{request.profile}` will run {len(commands)} command(s). "
                f"Build type: `{request.build_type}`. "
                f"Target: `{request.build_target or 'default'}`."
            )
            self.preview.setPlainText("\n".join(format_command(command) for command in commands))
        except Exception as exc:
            self.summary.setText("Build command generation failed.")
            self.preview.setPlainText(f"Cannot build command list:\n{exc}")

    def copy_preview(self) -> None:
        clipboard_copy(self.preview.toPlainText())

    # ------------------------------------------------------------------ #
    # execution                                                          #
    # ------------------------------------------------------------------ #

    def run_build(self) -> None:
        try:
            commands = build_commands(self.request())
        except Exception as exc:
            self.output.emit(f"[build error] {exc}\n")
            return
        self.run_button.setEnabled(False)
        self.abort_button.setEnabled(True)
        try:
            self._runner.start_sequence(commands)
        except Exception as exc:
            self.output.emit(f"[build error] {exc}\n")
            self.run_button.setEnabled(True)
            self.abort_button.setEnabled(False)

    def _on_sequence_finished(self, code: int) -> None:
        self.run_button.setEnabled(True)
        self.abort_button.setEnabled(False)
        tag = "succeeded" if code == 0 else f"failed (exit {code})"
        self.output.emit(f"\n[build {tag}]\n")
