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
    INTERNAL_ASSET_LABEL,
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
        QScrollArea,
        QSizePolicy,
        QSpinBox,
        QSplitter,
        QTabWidget,
        QVBoxLayout,
        QWidget,
    )
except ImportError:
    sys.exit("PySide6 is required. Install it with: python3 -m pip install PySide6")


class FileRow(QWidget):
    def __init__(self, title: str, default: str = "", directory: bool = False, tooltip: str = "", parent: QWidget | None = None) -> None:
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
        current = str(Path.cwd()) if current_text == INTERNAL_ASSET_LABEL else (current_text or str(Path.cwd()))
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


def set_tooltip(widget: QWidget, tooltip: str) -> None:
    widget.setToolTip(tooltip)


def add_form_row(form: QFormLayout, label_text: str, widget: QWidget, tooltip: str) -> QLabel:
    label = QLabel(label_text)
    label.setToolTip(tooltip)
    set_tooltip(widget, tooltip)
    form.addRow(label, widget)
    return label


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
        self.summary = QLabel()
        self.summary.setWordWrap(True)
        layout.addWidget(QLabel("Build command preview"))
        layout.addWidget(self.summary)
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

        top_split = QSplitter(Qt.Horizontal)
        self.chart_list = QListWidget()
        self.chart_list.setMinimumWidth(220)
        self.chart_list.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Expanding)
        self.chart_list.setToolTip("Discovered chart entries. Zip packages and folders come from the chart scanner; selecting one pre-fills chart and internal asset defaults.")
        self.chart_list.itemSelectionChanged.connect(self.apply_chart_selection)
        top_split.addWidget(self.chart_list)

        right_host = QWidget()
        right = QVBoxLayout(right_host)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_body = QWidget()
        scroll_layout = QVBoxLayout(scroll_body)

        self.binary = FileRow("Binary", str(DEFAULT_BINARY), tooltip="Executable to launch, typically `cpp/build/phigros_render`.")
        self.chart = FileRow("Chart", "", tooltip="Chart input path. Accepts JSON, PEC, PHBC, zip/pez packages, or zip member references like `pack.zip:chart.json`.")
        self.config = FileRow("Config", str(DEFAULT_CONFIG if DEFAULT_CONFIG.exists() else ""), tooltip="Render config JSONC path passed as `--config`.")
        self.respack = FileRow("Respack", str(DEFAULT_RESPACK if DEFAULT_RESPACK.exists() else ""), tooltip="Resource pack ZIP passed as `--respack`.")
        self.audio = FileRow("Audio", "", tooltip="Optional BGM override passed as `--audio`. `<internal>` means the chart package provides it.")
        self.bg = FileRow("Background", "", tooltip="Optional background override passed as `--bg`. `<internal>` means the chart package provides it.")
        self.font = FileRow("Font", "", tooltip="Optional TTF font override passed as `--font`.")
        self.binary.edit.setPlaceholderText("cpp/build/phigros_render")
        self.chart.edit.setPlaceholderText("charts/MyChart/IN.json or pack.zip:chart.json")
        self.config.edit.setPlaceholderText("config/config.jsonc")
        self.respack.edit.setPlaceholderText("respack.zip")
        self.audio.edit.setPlaceholderText("Leave blank to auto-detect, or <internal> for packaged assets")
        self.bg.edit.setPlaceholderText("Leave blank to auto-detect, or <internal> for packaged assets")
        self.font.edit.setPlaceholderText("assets/cmdysj.ttf")
        for row in (self.binary, self.chart, self.config, self.respack, self.audio, self.bg):
            row.edit.textChanged.connect(self.refresh_preview)
            scroll_layout.addWidget(row)
        self.font.edit.textChanged.connect(self.refresh_preview)
        scroll_layout.addWidget(self.font)

        details_group = QGroupBox("Selected Chart")
        details_layout = QFormLayout(details_group)
        self.detail_name = QLabel("No chart selected")
        self.detail_path = QLabel("-")
        self.detail_path.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.detail_assets = QLabel("Audio: - | Background: -")
        self.detail_source = QLabel("Source: -")
        self.detail_hint = QLabel("Select a chart on the left to prefill launcher fields.")
        self.detail_hint.setWordWrap(True)
        details_layout.addRow("Name", self.detail_name)
        details_layout.addRow("Path", self.detail_path)
        details_layout.addRow("Assets", self.detail_assets)
        details_layout.addRow("Source", self.detail_source)
        details_layout.addRow("", self.detail_hint)
        scroll_layout.addWidget(details_group)

        tabs = QTabWidget()

        playback_tab = QWidget()
        playback_form = QFormLayout(playback_tab)
        self.mode = QComboBox()
        self.mode.addItems(["", "autoplay", "manual", "scriptplay"])
        self.backend = QComboBox()
        self.backend.addItems(["", "sdl", "sdl_hw", "sdl_sw"])
        self.duration = QLineEdit()
        self.width = QLineEdit()
        self.height = QLineEdit()
        self.audio_offset = QLineEdit()
        self.playback_speed = QLineEdit()
        self.duration.setPlaceholderText("e.g. 30")
        self.width.setPlaceholderText("1280")
        self.height.setPlaceholderText("720")
        self.audio_offset.setPlaceholderText("e.g. 15")
        self.playback_speed.setPlaceholderText("e.g. 1.25")
        self.scriptplay = FileRow("Scriptplay", "", tooltip="Path for `--scriptplay` or `--judge-script`; switches gameplay mode to `scriptplay`.")
        self.script = FileRow("ChartScript", "", tooltip="Path for `--script <file.chartscript.json>` playlist-style input.")
        self.save_replay = FileRow("Save replay", "", tooltip="Save replay from a `--play` session via `--save-replay`.")
        self.play_replay = FileRow("Play replay", "", tooltip="Replay a saved session via `--play-replay`.")
        add_form_row(playback_form, "Mode", self.mode, "CLI accepts `autoplay`, `manual`, or `scriptplay` via `--mode`.")
        add_form_row(playback_form, "Backend", self.backend, "Renderer backend choices from CLI help: `sdl`, `sdl_hw`, or `sdl_sw`.")
        add_form_row(playback_form, "Duration", self.duration, "Passed as `--duration <sec>`.")
        add_form_row(playback_form, "Width", self.width, "Window width override via `--width <px>`.")
        add_form_row(playback_form, "Height", self.height, "Window height override via `--height <px>`.")
        add_form_row(playback_form, "Audio offset ms", self.audio_offset, "Audio latency compensation via `--audio-offset <ms>`.")
        add_form_row(playback_form, "Playback speed", self.playback_speed, "Optional playback override via `--playback-speed <mul>`. Defaults to chart-speed.")
        for row in (self.scriptplay, self.script, self.save_replay, self.play_replay):
            row.edit.textChanged.connect(self.refresh_preview)
            playback_form.addRow(row)
        tabs.addTab(playback_tab, "Playback")

        render_tab = QWidget()
        render_form = QFormLayout(render_tab)
        self.screenshot_dir = FileRow("Screenshots", "", directory=True, tooltip="Directory for `--screenshot-dir` periodic PNG output.")
        self.screenshot_fps = QLineEdit()
        self.debug_flags = QLineEdit()
        self.screenshot_fps.setPlaceholderText("0.2")
        self.debug_flags.setPlaceholderText("FRAME_TIME|AUDIO_INFO")
        add_form_row(render_form, "Screenshot FPS", self.screenshot_fps, "Chart-time screenshot rate via `--screenshot-fps <fps>`. Default 0.2.")
        add_form_row(render_form, "Debug flags", self.debug_flags, "Pipe or comma separated flags for `--debug-flags`, e.g. `FRAME_TIME|AUDIO_INFO`.")
        self.screenshot_dir.edit.textChanged.connect(self.refresh_preview)
        render_form.addRow(self.screenshot_dir)
        tabs.addTab(render_tab, "Render")

        record_tab = QWidget()
        record_form = QFormLayout(record_tab)
        self.record_output = FileRow("Record", "", tooltip="Output video path for `--record <output.mp4>`.")
        self.record_preset = QComboBox()
        self.record_preset.addItems(["", "fast", "balanced", "quality", "archive"])
        self.record_codec = QComboBox()
        self.record_codec.addItems(["", "libx264", "libx265", "libvpx-vp9", "h264_nvenc", "hevc_nvenc", "h264_qsv", "h264_vaapi"])
        self.record_hw = QComboBox()
        self.record_hw.addItems(["", "nvenc", "qsv", "vaapi", "amf", "videotoolbox"])
        self.record_fps = QLineEdit()
        self.sim_fps = QLineEdit()
        self.record_resolution = QLineEdit()
        self.record_capture_resolution = QLineEdit()
        self.record_queue_depth = QLineEdit()
        self.record_start = QLineEdit()
        self.record_end = QLineEdit()
        self.record_fps.setPlaceholderText("60")
        self.sim_fps.setPlaceholderText("defaults to record fps")
        self.record_resolution.setPlaceholderText("1920x1080")
        self.record_capture_resolution.setPlaceholderText("1920x1080")
        self.record_queue_depth.setPlaceholderText("6")
        self.record_start.setPlaceholderText("0")
        self.record_end.setPlaceholderText("leave blank for full chart")
        self.compile_output = FileRow("Compile", "", tooltip="Output PHBC path for `--compile <out.phbc>`.")
        self.sample_rate = QLineEdit()
        self.sample_rate.setPlaceholderText("240")
        self.compress_algo = QComboBox()
        self.compress_algo.addItems(["", "zlib", "lzma"])
        self.encrypt_algo = QComboBox()
        self.encrypt_algo.addItems(["", "aes-gcm", "aes-cbc", "chacha20", "xor"])
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.EchoMode.Password)
        self.password.setPlaceholderText("Required for encrypted PHBC")
        for label, widget, tip in [
            ("Record preset", self.record_preset, "Accepted `--record-preset` values: fast, balanced, quality, archive."),
            ("Record codec", self.record_codec, "Accepted `--record-codec` values from CLI help."),
            ("Record hardware", self.record_hw, "Accepted `--record-hw` values: nvenc, qsv, vaapi, amf, videotoolbox."),
            ("Record FPS", self.record_fps, "Output framerate for `--record-fps <fps>`."),
            ("Sim FPS", self.sim_fps, "Simulation sampling rate via `--sim-fps <fps>`."),
            ("Record resolution", self.record_resolution, "Format `WxH` for `--record-resolution`, for example `1920x1080`."),
            ("Capture resolution", self.record_capture_resolution, "Format `WxH` for `--record-capture-resolution`."),
            ("Queue depth", self.record_queue_depth, "Passed as `--record-queue-depth N`. `<=1` means synchronous writes."),
            ("Record start", self.record_start, "Passed as `--record-start <sec>`."),
            ("Record end", self.record_end, "Passed as `--record-end <sec>`."),
            ("Sample rate", self.sample_rate, "Compile sampling rate via `--sample-rate <Hz>`. Default 240."),
            ("Compress", self.compress_algo, "Accepted `--compress` choices: zlib or lzma."),
            ("Encrypt", self.encrypt_algo, "Accepted `--encrypt` choices: aes-gcm, aes-cbc, chacha20, xor."),
            ("Password", self.password, "Passphrase for encrypted PHBC read/write via `--password`."),
        ]:
            add_form_row(record_form, label, widget, tip)
            if isinstance(widget, QLineEdit):
                widget.textChanged.connect(self.refresh_preview)
            else:
                widget.currentTextChanged.connect(self.refresh_preview)
        for row in (self.record_output, self.compile_output):
            row.edit.textChanged.connect(self.refresh_preview)
            record_form.addRow(row)
        tabs.addTab(record_tab, "Record/Compile")

        logging_tab = QWidget()
        logging_form = QFormLayout(logging_tab)
        self.log_level = QComboBox()
        self.log_level.addItems(["", "trace", "debug", "info", "warn", "error", "fatal", "off"])
        self.log_filter = QLineEdit()
        self.log_file = QLineEdit()
        self.benchmark_iterations = QLineEdit()
        self.extra_args = QLineEdit()
        self.log_filter.setPlaceholderText("chart,render,audio")
        self.log_file.setPlaceholderText("logs/run.log")
        self.benchmark_iterations.setPlaceholderText("10")
        self.extra_args.setPlaceholderText("--approach 2.5 --chart-speed 1.1")
        add_form_row(logging_form, "Log level", self.log_level, "CLI log levels: trace, debug, info, warn, error, fatal, off.")
        add_form_row(logging_form, "Log filter", self.log_filter, "Comma-separated channel whitelist. CLI documents: general, chart, render, audio, record, engine, input, window, respack, compile, chartscript, mod, profile.")
        add_form_row(logging_form, "Log file", self.log_file, "Write log copy to this path with `--log-file`.")
        add_form_row(logging_form, "Benchmark iterations", self.benchmark_iterations, "Used with `--benchmark-iterations N`; default 10.")
        add_form_row(logging_form, "Extra args", self.extra_args, "Any additional raw CLI fragments appended after the modeled fields.")
        tabs.addTab(logging_tab, "Logging")

        for widget in (
            self.mode, self.backend, self.duration, self.width, self.height, self.audio_offset, self.playback_speed,
            self.screenshot_fps, self.debug_flags, self.log_level, self.log_filter, self.log_file, self.benchmark_iterations,
            self.extra_args,
        ):
            signal = widget.textChanged if isinstance(widget, QLineEdit) else widget.currentTextChanged
            signal.connect(self.refresh_preview)

        scroll_layout.addWidget(tabs)

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
        self.headless.setToolTip("Pass `--headless` to run without a visible window.")
        self.score_only.setToolTip("Pass `--score-only` for fast headless engine scoring.")
        self.benchmark.setToolTip("Pass `--benchmark`; implies `--score-only` in the CLI.")
        self.play_mode.setToolTip("Pass `--play` to force interactive/manual mode.")
        self.profile_mode.setToolTip("Pass `--profile` to print periodic frame timing stats.")
        self.record_profile.setToolTip("Pass `--record-profile` to print recording bottleneck stats.")
        self.overlay_transparent.setToolTip("Pass `--overlay-transparent` for lighter HUD/debug panels.")
        self.log_no_color.setToolTip("Pass `--log-no-color` to disable ANSI colors.")
        self.log_time.setToolTip("Pass `--log-time` to prepend timestamps.")
        self.truncate.setToolTip("Pass `--truncate-at-duration` so scoring only counts notes fully inside the duration window.")
        scroll_layout.addWidget(flags_group)
        scroll_layout.addStretch(1)
        scroll.setWidget(scroll_body)
        right.addWidget(scroll, 1)

        top_split.addWidget(right_host)
        top_split.setStretchFactor(0, 1)
        top_split.setStretchFactor(1, 3)
        layout.addWidget(top_split, 1)

        layout.addWidget(QLabel("Renderer command preview"))
        self.command_summary = QLabel()
        self.command_summary.setWordWrap(True)
        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        self.preview.setMaximumBlockCount(200)
        layout.addWidget(self.command_summary)
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
        charts = discover_charts(DEFAULT_CHARTS_DIR)
        for chart in charts:
            item = QListWidgetItem(chart["label"])
            item.setData(Qt.UserRole, chart)
            self.chart_list.addItem(item)
        self.detail_hint.setText(
            f"Discovered {len(charts)} chart entries from `{DEFAULT_CHARTS_DIR}`. "
            "Zip-packaged audio/background defaults show as `<internal>`."
        )

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
        self.detail_name.setText(payload["label"])
        self.detail_path.setText(payload["path"])
        self.detail_assets.setText(f"Audio: {payload['audio'] or '-'} | Background: {payload['bg'] or '-'}")
        source = "zip package" if ".zip" in payload["path"].lower() or ".pez" in payload["path"].lower() else "file/folder entry"
        self.detail_source.setText(source)
        self.detail_hint.setText("Field values were updated from the selected chart entry. You can still override any of them below.")
        self.refresh_preview()

    def command(self) -> list[str]:
        audio_path = self.audio.text()
        bg_path = self.bg.text()
        return launch_binary_command(
            binary=self.binary.text(),
            chart_path=self.chart.text(),
            respack=self.respack.text(),
            config_path=self.config.text(),
            bg_path="" if bg_path == INTERNAL_ASSET_LABEL else bg_path,
            font_path=self.font.text(),
            audio_path="" if audio_path == INTERNAL_ASSET_LABEL else audio_path,
            screenshot_dir=self.screenshot_dir.text(),
            screenshot_fps=self.screenshot_fps.text().strip(),
            duration=self.duration.text(),
            width=self.width.text(),
            height=self.height.text(),
            mode=self.mode.currentText(),
            backend=self.backend.currentText(),
            record_output=self.record_output.text(),
            record_preset=self.record_preset.currentText(),
            record_codec=self.record_codec.currentText(),
            record_hw=self.record_hw.currentText(),
            record_fps=self.record_fps.text().strip(),
            sim_fps=self.sim_fps.text().strip(),
            record_resolution=self.record_resolution.text().strip(),
            record_capture_resolution=self.record_capture_resolution.text().strip(),
            record_queue_depth=self.record_queue_depth.text().strip(),
            record_start=self.record_start.text().strip(),
            record_end=self.record_end.text().strip(),
            compile_output=self.compile_output.text(),
            sample_rate=self.sample_rate.text().strip(),
            compress_algo=self.compress_algo.currentText(),
            encrypt_algo=self.encrypt_algo.currentText(),
            password=self.password.text(),
            scriptplay_path=self.scriptplay.text(),
            script_path=self.script.text(),
            save_replay_path=self.save_replay.text(),
            play_replay_path=self.play_replay.text(),
            debug_flags=self.debug_flags.text().strip(),
            log_level=self.log_level.currentText(),
            log_filter=self.log_filter.text().strip(),
            log_file=self.log_file.text().strip(),
            audio_offset_ms=self.audio_offset.text().strip(),
            playback_speed=self.playback_speed.text().strip(),
            benchmark_iterations=self.benchmark_iterations.text().strip(),
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
            command = self.command()
            self.command_summary.setText(
                f"Command has {len(command)} argument token(s). "
                f"Mode: `{self.mode.currentText() or 'default'}`. "
                f"Headless: `{'yes' if self.headless.isChecked() else 'no'}`. "
                f"Recording: `{'yes' if bool(self.record_output.text()) else 'no'}`. "
                f"Compile-only: `{'yes' if bool(self.compile_output.text()) else 'no'}`."
            )
            self.preview.setPlainText(format_command(command))
        except Exception as exc:
            self.command_summary.setText("Renderer command generation failed.")
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
