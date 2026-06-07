"""Renderer tab — chart browser, CLI option editor, preset I/O, live launch.

Enhancements over the legacy launcher:
  * Chart list has a search box (filters by label).
  * Renderer fields are bound to :class:`RendererOptions` via a mapping, so
    future CLI flags only require one list edit.
  * Launch uses :class:`ProcessRunner` — non-blocking, streams to shared log.
  * Abort button kills the currently running renderer.
  * Presets: save/load the full option set to JSON in ``.phigros_ui/presets``.
  * State is persisted via :class:`QSettings` so paths survive restarts.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSettings,
    QSizePolicy,
    QSplitter,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from ..common import (
    DEFAULT_BINARY,
    DEFAULT_CHARTS_DIR,
    DEFAULT_CONFIG,
    DEFAULT_RESPACK,
    INTERNAL_ASSET_LABEL,
    RendererOptions,
    clipboard_copy,
    discover_charts,
    format_command,
)
from ..presets import delete_preset, list_presets, load_preset, save_preset
from ..process import ProcessRunner
from ..widgets import FileRow, add_form_row

_SETTINGS_CHARTS_DIR = "renderer/charts_dir"


class RendererTab(QWidget):
    """Main renderer-command builder and launcher."""

    output = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._runner = ProcessRunner(self)
        self._runner.output.connect(self.output.emit)
        self._runner.started.connect(lambda cmd: self.output.emit(f"\n$ {format_command(cmd)}\n"))
        self._runner.finished.connect(self._on_finished)
        self._runner.sequence_finished.connect(self._on_sequence_finished)

        root = QVBoxLayout(self)

        top_split = QSplitter(Qt.Horizontal)

        # ------------------------------------------------------------- #
        # left: chart list + search                                     #
        # ------------------------------------------------------------- #
        left_host = QWidget()
        left = QVBoxLayout(left_host)
        left.setContentsMargins(0, 0, 0, 0)

        charts_dir_row = QHBoxLayout()
        self._charts_dir_label = QLabel("Charts dir:")
        self._charts_dir_edit = QLineEdit()
        settings = QSettings()
        self._charts_dir_edit.setText(
            settings.value(_SETTINGS_CHARTS_DIR, str(DEFAULT_CHARTS_DIR))
        )
        self._charts_dir_edit.setPlaceholderText(str(DEFAULT_CHARTS_DIR))
        self._charts_dir_edit.setToolTip("Directory scanned for chart entries. Saved in QSettings.")
        self._charts_dir_browse = QPushButton("Browse…")
        self._charts_dir_browse.clicked.connect(self._browse_charts_dir)
        self._charts_dir_edit.textChanged.connect(self._on_charts_dir_changed)
        charts_dir_row.addWidget(self._charts_dir_label)
        charts_dir_row.addWidget(self._charts_dir_edit, 1)
        charts_dir_row.addWidget(self._charts_dir_browse)
        left.addLayout(charts_dir_row)

        self.chart_search = QLineEdit()
        self.chart_search.setPlaceholderText("Filter charts (substring match)…")
        self.chart_search.textChanged.connect(self._apply_chart_filter)
        left.addWidget(self.chart_search)
        self.chart_list = QListWidget()
        self.chart_list.setMinimumWidth(220)
        self.chart_list.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Expanding)
        self.chart_list.setToolTip(
            "Discovered chart entries. Zip packages and folders come from the chart scanner; "
            "selecting one pre-fills chart and internal asset defaults."
        )
        self.chart_list.itemSelectionChanged.connect(self.apply_chart_selection)
        left.addWidget(self.chart_list, 1)
        top_split.addWidget(left_host)

        # ------------------------------------------------------------- #
        # right: scrollable form                                         #
        # ------------------------------------------------------------- #
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
        for row in (self.binary, self.chart, self.config, self.respack, self.audio, self.bg, self.font):
            row.edit.textChanged.connect(self.refresh_preview)
            scroll_layout.addWidget(row)

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
        tabs.addTab(self._build_playback_tab(), "Playback")
        tabs.addTab(self._build_render_tab(), "Render")
        tabs.addTab(self._build_record_tab(), "Record/Compile")
        tabs.addTab(self._build_logging_tab(), "Logging")
        tabs.addTab(self._build_mods_tab(), "Mods")
        scroll_layout.addWidget(tabs)

        scroll_layout.addWidget(self._build_toggles_group())
        scroll_layout.addStretch(1)
        scroll.setWidget(scroll_body)
        right.addWidget(scroll, 1)

        top_split.addWidget(right_host)
        top_split.setStretchFactor(0, 1)
        top_split.setStretchFactor(1, 3)
        root.addWidget(top_split, 1)

        # --- preview & buttons -------------------------------------------- #
        root.addWidget(QLabel("Renderer command preview"))
        self.command_summary = QLabel()
        self.command_summary.setWordWrap(True)
        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        self.preview.setMaximumBlockCount(200)
        root.addWidget(self.command_summary)
        root.addWidget(self.preview, 1)

        buttons = QHBoxLayout()
        self.reload_charts = QPushButton("Reload Charts")
        self.copy_button = QPushButton("Copy Command")
        self.preset_save = QPushButton("Save Preset…")
        self.preset_combo = QComboBox()
        self.preset_combo.setMinimumWidth(160)
        self.preset_load = QPushButton("Load")
        self.preset_delete = QPushButton("Delete")
        self.launch_button = QPushButton("Launch Renderer")
        self.abort_button = QPushButton("Abort")
        self.abort_button.setEnabled(False)
        for btn in (
            self.reload_charts, self.copy_button, self.preset_save, self.preset_combo,
            self.preset_load, self.preset_delete, self.launch_button, self.abort_button,
        ):
            buttons.addWidget(btn)
        root.addLayout(buttons)

        self.reload_charts.clicked.connect(self.load_charts)
        self.copy_button.clicked.connect(self.copy_preview)
        self.launch_button.clicked.connect(self.launch_renderer)
        self.abort_button.clicked.connect(self._runner.abort)
        self.preset_save.clicked.connect(self._on_save_preset)
        self.preset_load.clicked.connect(self._on_load_preset)
        self.preset_delete.clicked.connect(self._on_delete_preset)

        self._charts: list[dict[str, str]] = []
        self._refresh_preset_combo()
        self.load_charts()
        self.refresh_preview()

    # ------------------------------------------------------------------ #
    # form-tab builders (wire widgets + refresh on change)               #
    # ------------------------------------------------------------------ #

    def _connect_refresh(self, widget: QWidget) -> None:
        if isinstance(widget, QLineEdit):
            widget.textChanged.connect(self.refresh_preview)
        elif isinstance(widget, QComboBox):
            widget.currentTextChanged.connect(self.refresh_preview)
        elif isinstance(widget, QCheckBox):
            widget.toggled.connect(self.refresh_preview)

    def _build_playback_tab(self) -> QWidget:
        tab = QWidget()
        form = QFormLayout(tab)
        self.mode = QComboBox(); self.mode.addItems(["", "autoplay", "manual", "scriptplay"])
        self.backend = QComboBox(); self.backend.addItems(["", "sdl", "sdl_hw", "sdl_sw"])
        self.duration = QLineEdit(); self.duration.setPlaceholderText("e.g. 30")
        self.width = QLineEdit(); self.width.setPlaceholderText("1280")
        self.height = QLineEdit(); self.height.setPlaceholderText("720")
        self.audio_offset = QLineEdit(); self.audio_offset.setPlaceholderText("e.g. 15")
        self.playback_speed = QLineEdit(); self.playback_speed.setPlaceholderText("e.g. 1.25")
        self.scriptplay = FileRow("Scriptplay", "", tooltip="Path for `--scriptplay` or `--judge-script`; switches gameplay mode to `scriptplay`.")
        self.script = FileRow("ChartScript", "", tooltip="Path for `--script <file.chartscript.json>` playlist-style input.")
        self.save_replay = FileRow("Save replay", "", tooltip="Save replay from a `--play` session via `--save-replay`.")
        self.play_replay = FileRow("Play replay", "", tooltip="Replay a saved session via `--play-replay`.")
        add_form_row(form, "Mode", self.mode, "CLI accepts `autoplay`, `manual`, or `scriptplay` via `--mode`.")
        add_form_row(form, "Backend", self.backend, "Renderer backend choices from CLI help: `sdl`, `sdl_hw`, or `sdl_sw`.")
        add_form_row(form, "Duration", self.duration, "Passed as `--duration <sec>`.")
        add_form_row(form, "Width", self.width, "Window width override via `--width <px>`.")
        add_form_row(form, "Height", self.height, "Window height override via `--height <px>`.")
        add_form_row(form, "Audio offset ms", self.audio_offset, "Audio latency compensation via `--audio-offset <ms>`.")
        add_form_row(form, "Playback speed", self.playback_speed, "Optional playback override via `--playback-speed <mul>`. Defaults to chart-speed.")
        for row in (self.scriptplay, self.script, self.save_replay, self.play_replay):
            row.edit.textChanged.connect(self.refresh_preview)
            form.addRow(row)
        for w in (self.mode, self.backend, self.duration, self.width, self.height, self.audio_offset, self.playback_speed):
            self._connect_refresh(w)
        return tab

    def _build_render_tab(self) -> QWidget:
        tab = QWidget()
        form = QFormLayout(tab)
        self.screenshot_dir = FileRow("Screenshots", "", directory=True, tooltip="Directory for `--screenshot-dir` periodic PNG output.")
        self.screenshot_fps = QLineEdit(); self.screenshot_fps.setPlaceholderText("0.2")
        self.approach = QLineEdit(); self.approach.setPlaceholderText("e.g. 3.0")
        self.chart_speed = QLineEdit(); self.chart_speed.setPlaceholderText("e.g. 1.0")
        self.expand = QLineEdit(); self.expand.setPlaceholderText("e.g. 1.0")
        self.note_scale_x = QLineEdit(); self.note_scale_x.setPlaceholderText("e.g. 1.0")
        self.note_scale_y = QLineEdit(); self.note_scale_y.setPlaceholderText("e.g. 1.0")
        self.note_alpha = QLineEdit(); self.note_alpha.setPlaceholderText("0.0 – 1.0")
        self.font_size = QLineEdit(); self.font_size.setPlaceholderText("e.g. 1.0")
        self.debug_flags = QLineEdit(); self.debug_flags.setPlaceholderText("FRAME_TIME|AUDIO_INFO")
        self.dump_frame_t = QLineEdit(); self.dump_frame_t.setPlaceholderText("e.g. 5.0")
        self.list_charts_dir = FileRow("List charts dir", "", directory=True, tooltip="Directory passed to `--list-charts <dir>` to enumerate chart metadata and exit.")
        rows_before_file = [
            ("Screenshot FPS", self.screenshot_fps, "Chart-time screenshot rate via `--screenshot-fps <fps>`. Default 0.2."),
            ("Approach time", self.approach, "Note approach duration in seconds via `--approach <sec>`."),
            ("Chart speed", self.chart_speed, "Chart speed multiplier via `--chart-speed <mul>`. Stacks with note speed."),
            ("Lane expand", self.expand, "Lane width expand factor via `--expand <factor>`."),
            ("Note scale X", self.note_scale_x, "Horizontal note scale multiplier via `--note-scale-x <mul>`."),
            ("Note scale Y", self.note_scale_y, "Vertical note scale multiplier via `--note-scale-y <mul>`."),
            ("Note alpha", self.note_alpha, "Note opacity in [0,1] via `--note-alpha <alpha>`."),
            ("Font size", self.font_size, "HUD font size multiplier via `--font-size <mul>`."),
            ("Debug flags", self.debug_flags, "Pipe or comma separated flags for `--debug-flags`, e.g. `FRAME_TIME|AUDIO_INFO`."),
            ("Dump frame at", self.dump_frame_t, "Render a single frame at this chart-time (seconds) and exit via `--dump-frame <t>`."),
        ]
        for label, widget, tip in rows_before_file:
            add_form_row(form, label, widget, tip)
            self._connect_refresh(widget)
        self.screenshot_dir.edit.textChanged.connect(self.refresh_preview)
        form.addRow(self.screenshot_dir)
        self.list_charts_dir.edit.textChanged.connect(self.refresh_preview)
        form.addRow(self.list_charts_dir)
        return tab

    def _build_record_tab(self) -> QWidget:
        tab = QWidget()
        form = QFormLayout(tab)
        self.record_output = FileRow("Record", "", tooltip="Output video path for `--record <output.mp4>`.")
        self.record_preset = QComboBox(); self.record_preset.addItems(["", "fast", "balanced", "quality", "archive"])
        self.record_codec = QComboBox(); self.record_codec.addItems(["", "libx264", "libx265", "libvpx-vp9", "h264_nvenc", "hevc_nvenc", "h264_qsv", "h264_vaapi"])
        self.record_hw = QComboBox(); self.record_hw.addItems(["", "nvenc", "qsv", "vaapi", "amf", "videotoolbox"])
        self.record_fps = QLineEdit(); self.record_fps.setPlaceholderText("60")
        self.sim_fps = QLineEdit(); self.sim_fps.setPlaceholderText("defaults to record fps")
        self.record_resolution = QLineEdit(); self.record_resolution.setPlaceholderText("1920x1080")
        self.record_capture_resolution = QLineEdit(); self.record_capture_resolution.setPlaceholderText("1920x1080")
        self.record_queue_depth = QLineEdit(); self.record_queue_depth.setPlaceholderText("6")
        self.record_start = QLineEdit(); self.record_start.setPlaceholderText("0")
        self.record_end = QLineEdit(); self.record_end.setPlaceholderText("leave blank for full chart")
        self.compile_output = FileRow("Compile", "", tooltip="Output PHBC path for `--compile <out.phbc>`.")
        self.sample_rate = QLineEdit(); self.sample_rate.setPlaceholderText("240")
        self.compress_algo = QComboBox(); self.compress_algo.addItems(["", "zlib", "lzma"])
        self.encrypt_algo = QComboBox(); self.encrypt_algo.addItems(["", "aes-gcm", "aes-cbc", "chacha20", "xor"])
        self.password = QLineEdit(); self.password.setEchoMode(QLineEdit.EchoMode.Password)
        self.password.setPlaceholderText("Required for encrypted PHBC")
        rows = [
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
        ]
        for label, widget, tip in rows:
            add_form_row(form, label, widget, tip)
            self._connect_refresh(widget)
        for row in (self.record_output, self.compile_output):
            row.edit.textChanged.connect(self.refresh_preview)
            form.addRow(row)
        return tab

    def _build_logging_tab(self) -> QWidget:
        tab = QWidget()
        form = QFormLayout(tab)
        self.log_level = QComboBox()
        self.log_level.addItems(["", "trace", "debug", "info", "warn", "error", "fatal", "off"])
        self.log_filter = QLineEdit(); self.log_filter.setPlaceholderText("chart,render,audio")
        self.log_file = QLineEdit(); self.log_file.setPlaceholderText("logs/run.log")
        self.benchmark_iterations = QLineEdit(); self.benchmark_iterations.setPlaceholderText("10")
        self.extra_args_field = QLineEdit()
        self.extra_args_field.setPlaceholderText("--approach 2.5 --chart-speed 1.1")
        add_form_row(form, "Log level", self.log_level, "CLI log levels: trace, debug, info, warn, error, fatal, off.")
        add_form_row(form, "Log filter", self.log_filter, "Comma-separated channel whitelist. CLI documents: general, chart, render, audio, record, engine, input, window, respack, compile, chartscript, mod, profile.")
        add_form_row(form, "Log file", self.log_file, "Write log copy to this path with `--log-file`.")
        add_form_row(form, "Benchmark iterations", self.benchmark_iterations, "Used with `--benchmark-iterations N`; default 10.")
        add_form_row(form, "Extra args", self.extra_args_field, "Any additional raw CLI fragments appended after the modeled fields.")
        for w in (self.log_level, self.log_filter, self.log_file, self.benchmark_iterations, self.extra_args_field):
            self._connect_refresh(w)
        return tab

    def _build_mods_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        info = QLabel(
            "List of `.mod.json` files to load via `--mod <file>` (one per entry). "
            "They are applied in order."
        )
        info.setWordWrap(True)
        layout.addWidget(info)
        self.mods_list = QListWidget()
        self.mods_list.setToolTip("Each entry is passed as `--mod <path>` to the renderer.")
        layout.addWidget(self.mods_list, 1)
        btns = QHBoxLayout()
        self.mods_add = QPushButton("Add…")
        self.mods_remove = QPushButton("Remove")
        self.mods_clear = QPushButton("Clear All")
        btns.addWidget(self.mods_add)
        btns.addWidget(self.mods_remove)
        btns.addWidget(self.mods_clear)
        btns.addStretch()
        layout.addLayout(btns)
        self.mods_add.clicked.connect(self._on_mod_add)
        self.mods_remove.clicked.connect(self._on_mod_remove)
        self.mods_clear.clicked.connect(self._on_mod_clear)
        return tab

    def _on_mod_add(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(
            self, "Select mod file(s)", "", "Mod files (*.mod.json *.json);;All files (*)"
        )
        for path in paths:
            if path.strip():
                self.mods_list.addItem(path.strip())
        if paths:
            self.refresh_preview()

    def _on_mod_remove(self) -> None:
        for item in self.mods_list.selectedItems():
            self.mods_list.takeItem(self.mods_list.row(item))
        self.refresh_preview()

    def _on_mod_clear(self) -> None:
        self.mods_list.clear()
        self.refresh_preview()

    def _browse_charts_dir(self) -> None:
        current = self._charts_dir_edit.text().strip() or str(DEFAULT_CHARTS_DIR)
        directory = QFileDialog.getExistingDirectory(self, "Select Charts Directory", current)
        if directory:
            self._charts_dir_edit.setText(directory)

    def _on_charts_dir_changed(self, text: str) -> None:
        QSettings().setValue(_SETTINGS_CHARTS_DIR, text.strip() or str(DEFAULT_CHARTS_DIR))
        self.load_charts()

    def _build_toggles_group(self) -> QGroupBox:
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
        self.info_mode = QCheckBox("Print chart info")
        tips = {
            self.headless: "Pass `--headless` to run without a visible window.",
            self.score_only: "Pass `--score-only` for fast headless engine scoring.",
            self.benchmark: "Pass `--benchmark`; implies `--score-only` in the CLI.",
            self.play_mode: "Pass `--play` to force interactive/manual mode.",
            self.profile_mode: "Pass `--profile` to print periodic frame timing stats.",
            self.record_profile: "Pass `--record-profile` to print recording bottleneck stats.",
            self.overlay_transparent: "Pass `--overlay-transparent` for lighter HUD/debug panels.",
            self.log_no_color: "Pass `--log-no-color` to disable ANSI colors.",
            self.log_time: "Pass `--log-time` to prepend timestamps.",
            self.truncate: "Pass `--truncate-at-duration` so scoring only counts notes fully inside the duration window.",
            self.info_mode: "Pass `--info` to print chart metadata and exit without rendering.",
        }
        for idx, (toggle, tip) in enumerate(tips.items()):
            toggle.setToolTip(tip)
            flags.addWidget(toggle, idx // 2, idx % 2)
            toggle.toggled.connect(self.refresh_preview)
        return flags_group

    # ------------------------------------------------------------------ #
    # model <-> widget bridge                                            #
    # ------------------------------------------------------------------ #

    #: Map RendererOptions field name -> (widget, accessor, setter).  Defined
    #: as a property because widgets are constructed in __init__.
    def _widget_map(self) -> dict[str, tuple[object, str, str]]:
        def get_text(w): return w.text().strip() if hasattr(w, "text") else ""
        # For QLineEdit.text()/QLineEdit.setText, QComboBox.currentText/setCurrentText,
        # QCheckBox.isChecked/setChecked, FileRow.text/setText.
        # We key each widget by its kind via isinstance elsewhere rather than via strings.
        return {}  # placeholder; actual binding done imperatively in options()/apply_options

    def options(self) -> RendererOptions:
        audio_path = self.audio.text()
        bg_path = self.bg.text()
        return RendererOptions(
            binary=self.binary.text(),
            chart_path=self.chart.text(),
            respack=self.respack.text(),
            config_path=self.config.text(),
            bg_path="" if bg_path == INTERNAL_ASSET_LABEL else bg_path,
            font_path=self.font.text(),
            audio_path="" if audio_path == INTERNAL_ASSET_LABEL else audio_path,
            screenshot_dir=self.screenshot_dir.text(),
            screenshot_fps=self.screenshot_fps.text().strip(),
            duration=self.duration.text().strip(),
            width=self.width.text().strip(),
            height=self.height.text().strip(),
            approach=self.approach.text().strip(),
            chart_speed=self.chart_speed.text().strip(),
            expand=self.expand.text().strip(),
            note_scale_x=self.note_scale_x.text().strip(),
            note_scale_y=self.note_scale_y.text().strip(),
            note_alpha=self.note_alpha.text().strip(),
            font_size=self.font_size.text().strip(),
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
            list_charts_dir=self.list_charts_dir.text(),
            dump_frame_t=self.dump_frame_t.text().strip(),
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
            info_mode=self.info_mode.isChecked(),
            mod_paths=[
                self.mods_list.item(i).text()
                for i in range(self.mods_list.count())
            ],
            extra_args=self.extra_args_field.text().strip(),
        )

    def apply_options(self, opts: RendererOptions) -> None:
        self.binary.setText(opts.binary)
        self.chart.setText(opts.chart_path)
        self.respack.setText(opts.respack)
        self.config.setText(opts.config_path)
        self.bg.setText(opts.bg_path)
        self.font.setText(opts.font_path)
        self.audio.setText(opts.audio_path)
        self.screenshot_dir.setText(opts.screenshot_dir)
        self.screenshot_fps.setText(opts.screenshot_fps)
        self.duration.setText(opts.duration)
        self.width.setText(opts.width)
        self.height.setText(opts.height)
        self.approach.setText(opts.approach)
        self.chart_speed.setText(opts.chart_speed)
        self.expand.setText(opts.expand)
        self.note_scale_x.setText(opts.note_scale_x)
        self.note_scale_y.setText(opts.note_scale_y)
        self.note_alpha.setText(opts.note_alpha)
        self.font_size.setText(opts.font_size)
        self.mode.setCurrentText(opts.mode)
        self.backend.setCurrentText(opts.backend)
        self.record_output.setText(opts.record_output)
        self.record_preset.setCurrentText(opts.record_preset)
        self.record_codec.setCurrentText(opts.record_codec)
        self.record_hw.setCurrentText(opts.record_hw)
        self.record_fps.setText(opts.record_fps)
        self.sim_fps.setText(opts.sim_fps)
        self.record_resolution.setText(opts.record_resolution)
        self.record_capture_resolution.setText(opts.record_capture_resolution)
        self.record_queue_depth.setText(opts.record_queue_depth)
        self.record_start.setText(opts.record_start)
        self.record_end.setText(opts.record_end)
        self.compile_output.setText(opts.compile_output)
        self.sample_rate.setText(opts.sample_rate)
        self.compress_algo.setCurrentText(opts.compress_algo)
        self.encrypt_algo.setCurrentText(opts.encrypt_algo)
        self.password.setText(opts.password)
        self.scriptplay.setText(opts.scriptplay_path)
        self.script.setText(opts.script_path)
        self.save_replay.setText(opts.save_replay_path)
        self.play_replay.setText(opts.play_replay_path)
        self.debug_flags.setText(opts.debug_flags)
        self.log_level.setCurrentText(opts.log_level)
        self.log_filter.setText(opts.log_filter)
        self.log_file.setText(opts.log_file)
        self.audio_offset.setText(opts.audio_offset_ms)
        self.playback_speed.setText(opts.playback_speed)
        self.benchmark_iterations.setText(opts.benchmark_iterations)
        self.list_charts_dir.setText(opts.list_charts_dir)
        self.dump_frame_t.setText(opts.dump_frame_t)
        self.headless.setChecked(opts.headless)
        self.score_only.setChecked(opts.score_only)
        self.benchmark.setChecked(opts.benchmark)
        self.play_mode.setChecked(opts.play_mode)
        self.profile_mode.setChecked(opts.profile_mode)
        self.record_profile.setChecked(opts.record_profile)
        self.overlay_transparent.setChecked(opts.overlay_transparent)
        self.log_no_color.setChecked(opts.log_no_color)
        self.log_time.setChecked(opts.log_time)
        self.truncate.setChecked(opts.truncate_at_duration)
        self.info_mode.setChecked(opts.info_mode)
        self.mods_list.clear()
        for path in (opts.mod_paths or []):
            self.mods_list.addItem(path)
        self.extra_args_field.setText(opts.extra_args)

    # ------------------------------------------------------------------ #
    # chart list handling                                                #
    # ------------------------------------------------------------------ #

    def load_charts(self) -> None:
        charts_dir_str = self._charts_dir_edit.text().strip() if hasattr(self, "_charts_dir_edit") else ""
        from pathlib import Path as _Path
        charts_dir = _Path(charts_dir_str) if charts_dir_str else DEFAULT_CHARTS_DIR
        self._charts = discover_charts(charts_dir)
        self._apply_chart_filter()
        self.detail_hint.setText(
            f"Discovered {len(self._charts)} chart entries from `{charts_dir}`. "
            "Zip-packaged audio/background defaults show as `<internal>`."
        )

    def _apply_chart_filter(self) -> None:
        needle = self.chart_search.text().strip().lower()
        self.chart_list.clear()
        for chart in self._charts:
            if needle and needle not in chart["label"].lower():
                continue
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
        self.detail_name.setText(payload["label"])
        self.detail_path.setText(payload["path"])
        self.detail_assets.setText(f"Audio: {payload['audio'] or '-'} | Background: {payload['bg'] or '-'}")
        path_lower = payload["path"].lower()
        source = "zip package" if ".zip" in path_lower or ".pez" in path_lower else "file/folder entry"
        self.detail_source.setText(source)
        self.detail_hint.setText("Field values were updated from the selected chart entry. You can still override any of them below.")
        self.refresh_preview()

    # ------------------------------------------------------------------ #
    # preview & launch                                                   #
    # ------------------------------------------------------------------ #

    def command(self) -> list[str]:
        return self.options().build_command()

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
        self.launch_button.setEnabled(False)
        self.abort_button.setEnabled(True)
        try:
            self._runner.start(command)
        except Exception as exc:
            QMessageBox.critical(self, "Launch Error", str(exc))
            self.launch_button.setEnabled(True)
            self.abort_button.setEnabled(False)

    def _on_finished(self, code: int, reason: str) -> None:
        self.output.emit(f"[renderer {reason} exit={code}]\n")

    def _on_sequence_finished(self, code: int) -> None:
        self.launch_button.setEnabled(True)
        self.abort_button.setEnabled(False)

    # ------------------------------------------------------------------ #
    # presets                                                            #
    # ------------------------------------------------------------------ #

    def _refresh_preset_combo(self) -> None:
        self.preset_combo.blockSignals(True)
        self.preset_combo.clear()
        names = list_presets()
        if not names:
            self.preset_combo.addItem("(no presets)")
            self.preset_combo.setEnabled(False)
            self.preset_load.setEnabled(False)
            self.preset_delete.setEnabled(False)
        else:
            self.preset_combo.addItems(names)
            self.preset_combo.setEnabled(True)
            self.preset_load.setEnabled(True)
            self.preset_delete.setEnabled(True)
        self.preset_combo.blockSignals(False)

    def _on_save_preset(self) -> None:
        name, ok = QInputDialog.getText(self, "Save Preset", "Preset name:")
        if not ok or not name.strip():
            return
        try:
            path = save_preset(name, self.options())
        except Exception as exc:
            QMessageBox.critical(self, "Save Preset", str(exc))
            return
        self._refresh_preset_combo()
        idx = self.preset_combo.findText(Path(path).stem)
        if idx >= 0:
            self.preset_combo.setCurrentIndex(idx)
        self.output.emit(f"[preset saved] {path}\n")

    def _on_load_preset(self) -> None:
        name = self.preset_combo.currentText()
        if not name or name == "(no presets)":
            return
        try:
            opts = load_preset(name)
        except Exception as exc:
            QMessageBox.critical(self, "Load Preset", str(exc))
            return
        self.apply_options(opts)
        self.refresh_preview()
        self.output.emit(f"[preset loaded] {name}\n")

    def _on_delete_preset(self) -> None:
        name = self.preset_combo.currentText()
        if not name or name == "(no presets)":
            return
        confirm = QMessageBox.question(self, "Delete Preset", f"Delete preset `{name}`?")
        if confirm != QMessageBox.Yes:
            return
        delete_preset(name)
        self._refresh_preset_combo()
        self.output.emit(f"[preset deleted] {name}\n")
