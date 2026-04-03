#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from launcher_common import (
    BUILD_PROFILES,
    DEFAULT_BINARY,
    DEFAULT_CHARTS_DIR,
    DEFAULT_CONFIG,
    DEFAULT_RESPACK,
    BuildRequest,
    build_commands,
    clipboard_copy,
    cpu_jobs,
    discover_charts,
    format_command,
    launch_binary_command,
)

try:
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import (
        QApplication,
        QCheckBox,
        QComboBox,
        QFileDialog,
        QFormLayout,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QListWidget,
        QListWidgetItem,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QPlainTextEdit,
        QSpinBox,
        QTabWidget,
        QVBoxLayout,
        QWidget,
    )
except ImportError:
    sys.exit("PySide6 is required. Install it with: python3 -m pip install PySide6")


class FileRow(QWidget):
    def __init__(self, title: str, default: str = "", directory: bool = False, parent: QWidget | None = None) -> None:
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

    def browse(self) -> None:
        current = self.edit.text().strip() or str(Path.cwd())
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


class BuildTab(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
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

        form.addRow("Profile", self.profile)
        form.addRow("Build type", self.build_type)
        form.addRow("Parallel jobs", self.jobs)
        form.addRow("Target override", self.target)
        form.addRow("Android ABI", self.android_abi)
        form.addRow("Android API", self.android_api)
        form.addRow("Extra CMake args", self.extra_args)
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
        checkboxes = [
            self.clean,
            self.use_bgfx,
            self.use_sdl3,
            self.use_libav,
            self.use_lzma,
            self.use_encryption,
            self.use_sanitizers,
            self.run_tests,
        ]
        for idx, checkbox in enumerate(checkboxes):
            flags_layout.addWidget(checkbox, idx // 2, idx % 2)
        layout.addWidget(flags_group)

        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        layout.addWidget(QLabel("Build command preview"))
        layout.addWidget(self.preview, 1)

        buttons = QHBoxLayout()
        self.copy_button = QPushButton("Copy Commands")
        self.run_button = QPushButton("Run Build")
        buttons.addWidget(self.copy_button)
        buttons.addWidget(self.run_button)
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
        self.refresh_preview()

    def request(self) -> BuildRequest:
        import shlex

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
            self.preview.setPlainText("\n".join(format_command(command) for command in commands))
        except Exception as exc:
            self.preview.setPlainText(f"Cannot build command list:\n{exc}")

    def copy_preview(self) -> None:
        clipboard_copy(self.preview.toPlainText())

    def run_build(self) -> None:
        try:
            commands = build_commands(self.request())
        except Exception as exc:
            QMessageBox.critical(self, "Build Error", str(exc))
            return
        for command in commands:
            result = subprocess.run(command)
            if result.returncode != 0:
                QMessageBox.critical(self, "Build Failed", f"Command failed:\n{format_command(command)}")
                return
        QMessageBox.information(self, "Build Complete", "Build commands finished successfully.")


class RendererTab(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)

        top = QHBoxLayout()
        self.chart_list = QListWidget()
        self.chart_list.setMinimumWidth(260)
        self.chart_list.itemSelectionChanged.connect(self.apply_chart_selection)
        top.addWidget(self.chart_list)

        right = QVBoxLayout()
        self.binary = FileRow("Binary", str(DEFAULT_BINARY))
        self.chart = FileRow("Chart", "")
        self.config = FileRow("Config", str(DEFAULT_CONFIG if DEFAULT_CONFIG.exists() else ""))
        self.respack = FileRow("Respack", str(DEFAULT_RESPACK if DEFAULT_RESPACK.exists() else ""))
        self.audio = FileRow("Audio", "")
        self.bg = FileRow("Background", "")
        for row in (self.binary, self.chart, self.config, self.respack, self.audio, self.bg):
            row.edit.textChanged.connect(self.refresh_preview)
            right.addWidget(row)

        options_group = QGroupBox("CLI Options")
        options = QGridLayout(options_group)
        self.mode = QComboBox()
        self.mode.addItems(["", "autoplay", "manual", "scriptplay"])
        self.backend = QLineEdit()
        self.duration = QLineEdit()
        self.width = QLineEdit()
        self.height = QLineEdit()
        self.audio_offset = QLineEdit()
        self.playback_speed = QLineEdit()
        self.scriptplay = FileRow("Scriptplay", "")
        self.script = FileRow("ChartScript", "")
        self.record_output = FileRow("Record", "")
        self.compile_output = FileRow("Compile", "")
        self.debug_flags = QLineEdit()
        self.log_level = QComboBox()
        self.log_level.addItems(["", "trace", "debug", "info", "warn", "error", "fatal", "off"])
        self.log_filter = QLineEdit()
        self.log_file = QLineEdit()
        self.extra_args = QLineEdit()

        widgets = [
            ("Mode", self.mode),
            ("Backend", self.backend),
            ("Duration", self.duration),
            ("Width", self.width),
            ("Height", self.height),
            ("Audio offset ms", self.audio_offset),
            ("Playback speed", self.playback_speed),
            ("Debug flags", self.debug_flags),
            ("Log level", self.log_level),
            ("Log filter", self.log_filter),
            ("Log file", self.log_file),
            ("Extra args", self.extra_args),
        ]
        for idx, (label, widget) in enumerate(widgets):
            options.addWidget(QLabel(label), idx, 0)
            options.addWidget(widget, idx, 1)
            if isinstance(widget, (QLineEdit, QComboBox)):
                signal = widget.textChanged if isinstance(widget, QLineEdit) else widget.currentTextChanged
                signal.connect(self.refresh_preview)
        right.addWidget(options_group)

        advanced_rows = (self.scriptplay, self.script, self.record_output, self.compile_output)
        for row in advanced_rows:
            row.edit.textChanged.connect(self.refresh_preview)
            right.addWidget(row)

        flags_group = QGroupBox("Toggles")
        flags = QGridLayout(flags_group)
        self.headless = QCheckBox("Headless")
        self.score_only = QCheckBox("Score only")
        self.benchmark = QCheckBox("Benchmark")
        self.play_mode = QCheckBox("Interactive play")
        self.profile_mode = QCheckBox("Profile timings")
        self.record_profile = QCheckBox("Record profile")
        self.overlay_transparent = QCheckBox("Transparent overlay")
        self.log_no_color = QCheckBox("No log color")
        self.log_time = QCheckBox("Log timestamps")
        self.truncate = QCheckBox("Truncate at duration")
        toggles = [
            self.headless,
            self.score_only,
            self.benchmark,
            self.play_mode,
            self.profile_mode,
            self.record_profile,
            self.overlay_transparent,
            self.log_no_color,
            self.log_time,
            self.truncate,
        ]
        for idx, toggle in enumerate(toggles):
            flags.addWidget(toggle, idx // 2, idx % 2)
            toggle.toggled.connect(self.refresh_preview)
        right.addWidget(flags_group)

        top.addLayout(right, 1)
        layout.addLayout(top, 1)

        layout.addWidget(QLabel("Renderer command preview"))
        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        layout.addWidget(self.preview, 1)

        buttons = QHBoxLayout()
        self.reload_charts = QPushButton("Reload Charts")
        self.copy_button = QPushButton("Copy Command")
        self.launch_button = QPushButton("Launch Renderer")
        buttons.addWidget(self.reload_charts)
        buttons.addWidget(self.copy_button)
        buttons.addWidget(self.launch_button)
        layout.addLayout(buttons)

        self.reload_charts.clicked.connect(self.load_charts)
        self.copy_button.clicked.connect(self.copy_preview)
        self.launch_button.clicked.connect(self.launch_renderer)
        self.load_charts()
        self.refresh_preview()

    def load_charts(self) -> None:
        self.chart_list.clear()
        for chart in discover_charts(DEFAULT_CHARTS_DIR):
            item = QListWidgetItem(chart["label"])
            item.setData(Qt.UserRole, chart)
            self.chart_list.addItem(item)

    def apply_chart_selection(self) -> None:
        item = self.chart_list.currentItem()
        if not item:
            return
        payload = item.data(Qt.UserRole)
        self.chart.setText(payload["path"])
        if payload["audio"]:
            self.audio.setText(payload["audio"])
        if payload["bg"]:
            self.bg.setText(payload["bg"])
        self.refresh_preview()

    def command(self) -> list[str]:
        return launch_binary_command(
            binary=self.binary.text(),
            chart_path=self.chart.text(),
            respack=self.respack.text(),
            config_path=self.config.text(),
            bg_path=self.bg.text(),
            audio_path=self.audio.text(),
            duration=self.duration.text(),
            width=self.width.text(),
            height=self.height.text(),
            mode=self.mode.currentText(),
            backend=self.backend.text().strip(),
            record_output=self.record_output.text(),
            compile_output=self.compile_output.text(),
            scriptplay_path=self.scriptplay.text(),
            script_path=self.script.text(),
            debug_flags=self.debug_flags.text().strip(),
            log_level=self.log_level.currentText(),
            log_filter=self.log_filter.text().strip(),
            log_file=self.log_file.text().strip(),
            audio_offset_ms=self.audio_offset.text().strip(),
            playback_speed=self.playback_speed.text().strip(),
            headless=self.headless.isChecked(),
            score_only=self.score_only.isChecked(),
            benchmark=self.benchmark.isChecked(),
            play_mode=self.play_mode.isChecked(),
            profile_mode=self.profile_mode.isChecked(),
            record_profile=self.record_profile.isChecked(),
            overlay_transparent=self.overlay_transparent.isChecked(),
            log_no_color=self.log_no_color.isChecked(),
            log_time=self.log_time.isChecked(),
            truncate_at_duration=self.truncate.isChecked(),
            extra_args=self.extra_args.text().strip(),
        )

    def refresh_preview(self) -> None:
        try:
            self.preview.setPlainText(format_command(self.command()))
        except Exception as exc:
            self.preview.setPlainText(str(exc))

    def copy_preview(self) -> None:
        clipboard_copy(self.preview.toPlainText())

    def launch_renderer(self) -> None:
        try:
            command = self.command()
        except Exception as exc:
            QMessageBox.critical(self, "Launch Error", str(exc))
            return
        result = subprocess.Popen(command)
        QMessageBox.information(self, "Renderer Started", f"Started PID {result.pid}")


class LauncherWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("MinimalPhigrosRend Launcher")
        self.resize(1280, 860)

        tabs = QTabWidget()
        tabs.addTab(BuildTab(), "Build")
        tabs.addTab(RendererTab(), "Renderer CLI")
        self.setCentralWidget(tabs)


def main() -> int:
    app = QApplication(sys.argv)
    window = LauncherWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
