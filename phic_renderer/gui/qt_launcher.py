from __future__ import annotations

import json
import os
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from typing import Any, Optional

from ..assets.loader import load_chart
from ..assets.respack import _parse_info_yml_minimal
from ..api.playlist import _resolve_pack_or_chart, discover_chart_inputs
from ..config_v2 import dump_config_v2, flatten_config_v2, load_config_v2


class ChartCardWidget:
    def __init__(self, qt: Any, title: str, bg_label: str, bgm_label: str, width: int = 480, height: int = 120):
        self.qt = qt
        QtWidgets = qt.QtWidgets
        QtCore = qt.QtCore
        QtGui = qt.QtGui
        
        self.widget = QtWidgets.QWidget()
        self.widget.setFixedSize(width, height)
        
        self._bg_pixmap: Optional[Any] = None
        self._title = title
        self._bg_label = bg_label
        self._bgm_label = bgm_label
        
        layout = QtWidgets.QVBoxLayout(self.widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        self.canvas = QtWidgets.QLabel()
        self.canvas.setFixedSize(width, height)
        layout.addWidget(self.canvas)
        
        self._render()
    
    def set_background(self, pixmap: Any) -> None:
        self._bg_pixmap = pixmap
        self._render()
    
    def _render(self) -> None:
        QtGui = self.qt.QtGui
        QtCore = self.qt.QtCore
        
        w = self.canvas.width()
        h = self.canvas.height()
        
        canvas_pm = QtGui.QPixmap(w, h)
        canvas_pm.fill(QtGui.QColor(30, 30, 35))
        
        painter = QtGui.QPainter(canvas_pm)
        try:
            painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        except Exception:
            pass
        
        if self._bg_pixmap is not None:
            try:
                scaled = self._bg_pixmap.scaled(
                    w, h,
                    QtCore.Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                    QtCore.Qt.TransformationMode.SmoothTransformation
                )
                x_off = (scaled.width() - w) // 2
                y_off = (scaled.height() - h) // 2
                painter.drawPixmap(0, 0, scaled, x_off, y_off, w, h)
                
                painter.fillRect(0, 0, w, h, QtGui.QColor(0, 0, 0, 140))
            except Exception:
                pass
        
        painter.setPen(QtGui.QColor(255, 255, 255, 255))
        try:
            font = painter.font()
            font.setPointSize(12)
            font.setBold(True)
            painter.setFont(font)
        except Exception:
            pass
        
        title_rect = QtCore.QRect(12, 12, w - 24, 30)
        try:
            painter.drawText(title_rect, QtCore.Qt.AlignmentFlag.AlignLeft | QtCore.Qt.AlignmentFlag.AlignTop, self._title)
        except Exception:
            painter.drawText(title_rect, 0x0001 | 0x0020, self._title)
        
        painter.setPen(QtGui.QColor(200, 200, 200, 230))
        try:
            font = painter.font()
            font.setPointSize(9)
            font.setBold(False)
            painter.setFont(font)
        except Exception:
            pass
        
        y_pos = 50
        if self._bgm_label:
            info_rect = QtCore.QRect(12, y_pos, w - 24, 20)
            text = f"🎵 {self._bgm_label}"
            try:
                painter.drawText(info_rect, QtCore.Qt.AlignmentFlag.AlignLeft | QtCore.Qt.AlignmentFlag.AlignVCenter, text)
            except Exception:
                painter.drawText(info_rect, 0x0001 | 0x0080, text)
            y_pos += 22
        
        if self._bg_label:
            info_rect = QtCore.QRect(12, y_pos, w - 24, 20)
            text = f"🖼️ {self._bg_label}"
            try:
                painter.drawText(info_rect, QtCore.Qt.AlignmentFlag.AlignLeft | QtCore.Qt.AlignmentFlag.AlignVCenter, text)
            except Exception:
                painter.drawText(info_rect, 0x0001 | 0x0080, text)
        
        painter.end()
        self.canvas.setPixmap(canvas_pm)


@dataclass
class _Qt:
    QtCore: Any
    QtGui: Any
    QtWidgets: Any
    api_name: str


def _import_qt() -> _Qt:
    try:
        from PySide6 import QtCore, QtGui, QtWidgets  # type: ignore

        return _Qt(QtCore=QtCore, QtGui=QtGui, QtWidgets=QtWidgets, api_name="PySide6")
    except Exception:
        pass

    try:
        from PyQt6 import QtCore, QtGui, QtWidgets  # type: ignore

        return _Qt(QtCore=QtCore, QtGui=QtGui, QtWidgets=QtWidgets, api_name="PyQt6")
    except Exception as e:
        raise SystemExit(
            "Qt launcher requires PySide6 or PyQt6. Install one of:\n"
            "  pip install PySide6\n"
            "  pip install PyQt6\n"
            f"Import error: {e}"
        )


class LauncherWindow:
    def __init__(self, qt: _Qt):
        self.qt = qt
        QtWidgets = qt.QtWidgets
        QtGui = qt.QtGui

        self._thumb_size = (160, 90)

        self.w = QtWidgets.QMainWindow()
        self.w.setWindowTitle("Mini Phigros Renderer Launcher")
        try:
            self.w.resize(980, 780)
        except Exception:
            pass

        central = QtWidgets.QWidget()
        self.w.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)

        self._preview_debounce = qt.QtCore.QTimer()
        self._preview_debounce.setSingleShot(True)
        self._preview_debounce.setInterval(250)
        self._preview_debounce.timeout.connect(self._refresh_preview)

        self._thumb_debounce = qt.QtCore.QTimer()
        self._thumb_debounce.setSingleShot(True)
        try:
            self._thumb_debounce.setInterval(60)
        except Exception:
            pass
        self._thumb_debounce.timeout.connect(self._thumb_load_tick)
        self._thumb_load_gen = 0
        self._thumb_pending: list[Any] = []

        self.pending_tokens: Optional[list[str]] = None

        self.tabs = QtWidgets.QTabWidget()
        root.addWidget(self.tabs)

        tab_launch = QtWidgets.QWidget()
        tab_preview = QtWidgets.QWidget()
        self.tabs.addTab(tab_launch, "Launch")
        self.tabs.addTab(tab_preview, "Preview")

        launch_outer = QtWidgets.QVBoxLayout(tab_launch)
        try:
            launch_outer.setContentsMargins(12, 12, 12, 12)
            launch_outer.setSpacing(10)
        except Exception:
            pass

        launch_scroll = QtWidgets.QScrollArea()
        launch_scroll.setWidgetResizable(True)
        try:
            launch_scroll.setAlignment(qt.QtCore.Qt.AlignmentFlag.AlignTop | qt.QtCore.Qt.AlignmentFlag.AlignLeft)
        except Exception:
            pass
        launch_outer.addWidget(launch_scroll)

        launch_scroll_w = QtWidgets.QWidget()
        try:
            sp = QtWidgets.QSizePolicy(QtWidgets.QSizePolicy.Policy.Expanding, QtWidgets.QSizePolicy.Policy.Maximum)
            launch_scroll_w.setSizePolicy(sp)
        except Exception:
            pass
        launch_scroll.setWidget(launch_scroll_w)
        launch_root = QtWidgets.QVBoxLayout(launch_scroll_w)
        try:
            launch_root.setContentsMargins(0, 0, 0, 0)
            launch_root.setSpacing(0)
        except Exception:
            pass

        self.toolbox = QtWidgets.QToolBox()
        launch_root.addWidget(self.toolbox)

        page_input = QtWidgets.QWidget()
        self.toolbox.addItem(page_input, "📂 Input & Charts")
        page_input_layout = QtWidgets.QVBoxLayout(page_input)
        try:
            page_input_layout.setContentsMargins(12, 12, 12, 12)
            page_input_layout.setSpacing(12)
        except Exception:
            pass

        self.grp_input = QtWidgets.QGroupBox("Input")
        page_input_layout.addWidget(self.grp_input)
        form_input = QtWidgets.QFormLayout(self.grp_input)
        try:
            form_input.setContentsMargins(8, 12, 8, 8)
            form_input.setVerticalSpacing(8)
        except Exception:
            pass

        self.input_path = QtWidgets.QLineEdit()
        self.input_browse = QtWidgets.QPushButton("Browse")
        self.input_browse.clicked.connect(self._browse_input)
        input_row = QtWidgets.QHBoxLayout()
        input_row.addWidget(self.input_path)
        input_row.addWidget(self.input_browse)
        form_input.addRow("Input (--input)", input_row)

        self.advance_path = QtWidgets.QLineEdit()
        self.advance_browse = QtWidgets.QPushButton("Browse")
        self.advance_browse.clicked.connect(self._browse_advance)
        adv_row = QtWidgets.QHBoxLayout()
        adv_row.addWidget(self.advance_path)
        adv_row.addWidget(self.advance_browse)
        form_input.addRow("Advance (--advance)", adv_row)

        self.grp_library = QtWidgets.QGroupBox("Chart Library")
        page_input_layout.addWidget(self.grp_library, 1)
        lib_root = QtWidgets.QVBoxLayout(self.grp_library)
        try:
            lib_root.setContentsMargins(8, 12, 8, 8)
            lib_root.setSpacing(8)
        except Exception:
            pass

        lib_dir_row = QtWidgets.QHBoxLayout()
        self.charts_dir = QtWidgets.QLineEdit()
        self.charts_dir_browse = QtWidgets.QPushButton("Browse")
        self.charts_dir_browse.clicked.connect(self._browse_charts_dir)
        self.btn_scan_charts = QtWidgets.QPushButton("Scan")
        self.btn_scan_charts.clicked.connect(self._scan_charts_dir)
        lib_dir_row.addWidget(self.charts_dir, 1)
        lib_dir_row.addWidget(self.charts_dir_browse)
        lib_dir_row.addWidget(self.btn_scan_charts)
        lib_root.addLayout(lib_dir_row)

        self.chart_entries = QtWidgets.QListWidget()
        try:
            self.chart_entries.setSelectionMode(QtWidgets.QAbstractItemView.SelectionMode.SingleSelection)
        except Exception:
            pass
        try:
            self.chart_entries.setViewMode(QtWidgets.QListWidget.ViewMode.IconMode)
        except Exception:
            pass
        try:
            self.chart_entries.setResizeMode(QtWidgets.QListWidget.ResizeMode.Adjust)
        except Exception:
            pass
        try:
            self.chart_entries.setGridSize(qt.QtCore.QSize(640, 170))
        except Exception:
            pass
        try:
            self.chart_entries.setSpacing(8)
        except Exception:
            pass
        self.chart_entries.itemSelectionChanged.connect(self._on_chart_entry_selected)
        lib_root.addWidget(self.chart_entries, 1)

        try:
            sb = self.chart_entries.verticalScrollBar()
            sb.valueChanged.connect(self._schedule_thumb_load)
        except Exception:
            pass

        try:
            self.charts_dir.setText(self._default_charts_dir())
        except Exception:
            pass

        page_config = QtWidgets.QWidget()
        self.toolbox.addItem(page_config, "⚙️ Config & Backend")
        page_config_layout = QtWidgets.QVBoxLayout(page_config)
        try:
            page_config_layout.setContentsMargins(12, 12, 12, 12)
            page_config_layout.setSpacing(12)
        except Exception:
            pass

        self.grp_cfg = QtWidgets.QGroupBox("Config / CUI")
        page_config_layout.addWidget(self.grp_cfg)
        form_cfg = QtWidgets.QFormLayout(self.grp_cfg)
        try:
            form_cfg.setContentsMargins(8, 12, 8, 8)
            form_cfg.setVerticalSpacing(8)
        except Exception:
            pass

        self.lang = QtWidgets.QComboBox()
        self.lang.addItems(["", "zh-CN", "en"])
        form_cfg.addRow("Language (--lang)", self.lang)

        self.quiet = QtWidgets.QCheckBox("Quiet (--quiet)")
        form_cfg.addRow("", self.quiet)

        self.no_color = QtWidgets.QCheckBox("No color (--no_color)")
        form_cfg.addRow("", self.no_color)

        self.grp_backend = QtWidgets.QGroupBox("Backend / Window")
        page_config_layout.addWidget(self.grp_backend)
        form_backend = QtWidgets.QFormLayout(self.grp_backend)
        try:
            form_backend.setContentsMargins(8, 12, 8, 8)
            form_backend.setVerticalSpacing(8)
        except Exception:
            pass

        self.backend = QtWidgets.QComboBox()
        self.backend.addItems(["pygame", "moderngl"])
        form_backend.addRow("Backend (--backend)", self.backend)

        self.audio_backend = QtWidgets.QComboBox()
        self.audio_backend.addItems(["pygame", "openal"])
        form_backend.addRow("Audio (--audio_backend)", self.audio_backend)

        self.w_spin = QtWidgets.QSpinBox()
        self.w_spin.setRange(320, 10000)
        self.w_spin.setValue(1280)
        form_backend.addRow("Width (--w)", self.w_spin)

        self.h_spin = QtWidgets.QSpinBox()
        self.h_spin.setRange(240, 10000)
        self.h_spin.setValue(720)
        form_backend.addRow("Height (--h)", self.h_spin)

        self.expand_spin = QtWidgets.QDoubleSpinBox()
        self.expand_spin.setDecimals(3)
        self.expand_spin.setRange(0.1, 10.0)
        self.expand_spin.setSingleStep(0.1)
        self.expand_spin.setValue(1.0)
        form_backend.addRow("Expand (--expand)", self.expand_spin)

        page_config_layout.addStretch(1)

        page_assets = QtWidgets.QWidget()
        self.toolbox.addItem(page_assets, "🎨 Assets & Audio")
        page_assets_layout = QtWidgets.QVBoxLayout(page_assets)
        try:
            page_assets_layout.setContentsMargins(12, 12, 12, 12)
            page_assets_layout.setSpacing(12)
        except Exception:
            pass

        self.grp_assets = QtWidgets.QGroupBox("Assets")
        page_assets_layout.addWidget(self.grp_assets)
        form_assets = QtWidgets.QFormLayout(self.grp_assets)
        try:
            form_assets.setContentsMargins(8, 12, 8, 8)
            form_assets.setVerticalSpacing(8)
        except Exception:
            pass

        self.respack_path = QtWidgets.QLineEdit()
        self.respack_browse = QtWidgets.QPushButton("Browse")
        self.respack_browse.clicked.connect(self._browse_respack)
        rp_row = QtWidgets.QHBoxLayout()
        rp_row.addWidget(self.respack_path)
        rp_row.addWidget(self.respack_browse)
        form_assets.addRow("Respack (--respack)", rp_row)

        self.bg_path = QtWidgets.QLineEdit()
        self.bg_browse = QtWidgets.QPushButton("Browse")
        self.bg_browse.clicked.connect(self._browse_bg)
        bg_row = QtWidgets.QHBoxLayout()
        bg_row.addWidget(self.bg_path)
        bg_row.addWidget(self.bg_browse)
        form_assets.addRow("BG (--bg)", bg_row)

        self.bg_blur = QtWidgets.QSpinBox()
        self.bg_blur.setRange(0, 100)
        self.bg_blur.setValue(10)
        form_assets.addRow("BG blur (--bg_blur)", self.bg_blur)

        self.bg_dim = QtWidgets.QSpinBox()
        self.bg_dim.setRange(0, 255)
        self.bg_dim.setValue(120)
        form_assets.addRow("BG dim (--bg_dim)", self.bg_dim)

        self.grp_audio = QtWidgets.QGroupBox("Audio")
        page_assets_layout.addWidget(self.grp_audio)
        form_audio = QtWidgets.QFormLayout(self.grp_audio)
        try:
            form_audio.setContentsMargins(8, 12, 8, 8)
            form_audio.setVerticalSpacing(8)
        except Exception:
            pass

        self.bgm_path = QtWidgets.QLineEdit()
        self.bgm_browse = QtWidgets.QPushButton("Browse")
        self.bgm_browse.clicked.connect(self._browse_bgm)
        bgm_row = QtWidgets.QHBoxLayout()
        bgm_row.addWidget(self.bgm_path)
        bgm_row.addWidget(self.bgm_browse)
        form_audio.addRow("BGM (--bgm)", bgm_row)

        self.force_bgm = QtWidgets.QCheckBox("Force BGM override (--force)")
        form_audio.addRow("", self.force_bgm)

        self.bgm_volume = QtWidgets.QDoubleSpinBox()
        self.bgm_volume.setDecimals(3)
        self.bgm_volume.setRange(0.0, 1.0)
        self.bgm_volume.setSingleStep(0.05)
        self.bgm_volume.setValue(0.8)
        form_audio.addRow("BGM volume (--bgm_volume)", self.bgm_volume)

        page_assets_layout.addStretch(1)

        page_render = QtWidgets.QWidget()
        self.toolbox.addItem(page_render, "🎬 Render & Visual")
        page_render_layout = QtWidgets.QVBoxLayout(page_render)
        try:
            page_render_layout.setContentsMargins(12, 12, 12, 12)
            page_render_layout.setSpacing(12)
        except Exception:
            pass

        self.grp_render = QtWidgets.QGroupBox("Render")
        self.grp_render.setCheckable(True)
        self.grp_render.setChecked(True)
        page_render_layout.addWidget(self.grp_render)
        _render_outer = QtWidgets.QVBoxLayout(self.grp_render)
        try:
            _render_outer.setContentsMargins(8, 12, 8, 8)
            _render_outer.setSpacing(8)
        except Exception:
            pass
        self.grp_render_body = QtWidgets.QWidget()
        _render_outer.addWidget(self.grp_render_body)
        try:
            self.grp_render.toggled.connect(self.grp_render_body.setVisible)
        except Exception:
            pass
        form_render = QtWidgets.QFormLayout(self.grp_render_body)
        try:
            form_render.setVerticalSpacing(8)
        except Exception:
            pass

        self.chart_speed = QtWidgets.QDoubleSpinBox()
        self.chart_speed.setDecimals(3)
        self.chart_speed.setRange(0.05, 10.0)
        self.chart_speed.setSingleStep(0.1)
        self.chart_speed.setValue(1.0)
        form_render.addRow("Chart speed (--chart_speed)", self.chart_speed)

        self.approach = QtWidgets.QDoubleSpinBox()
        self.approach.setDecimals(3)
        self.approach.setRange(0.1, 30.0)
        self.approach.setSingleStep(0.25)
        self.approach.setValue(3.0)
        form_render.addRow("Approach (--approach)", self.approach)

        self.note_scale_x = QtWidgets.QDoubleSpinBox()
        self.note_scale_x.setDecimals(3)
        self.note_scale_x.setRange(0.1, 5.0)
        self.note_scale_x.setSingleStep(0.1)
        self.note_scale_x.setValue(1.0)
        form_render.addRow("Note scale X (--note_scale_x)", self.note_scale_x)

        self.note_scale_y = QtWidgets.QDoubleSpinBox()
        self.note_scale_y.setDecimals(3)
        self.note_scale_y.setRange(0.1, 5.0)
        self.note_scale_y.setSingleStep(0.1)
        self.note_scale_y.setValue(1.0)
        form_render.addRow("Note scale Y (--note_scale_y)", self.note_scale_y)

        self.note_flow_speed_multiplier = QtWidgets.QDoubleSpinBox()
        self.note_flow_speed_multiplier.setDecimals(3)
        self.note_flow_speed_multiplier.setRange(0.01, 10.0)
        self.note_flow_speed_multiplier.setSingleStep(0.1)
        self.note_flow_speed_multiplier.setValue(1.0)
        form_render.addRow("Flow speed mul (--note_flow_speed_multiplier)", self.note_flow_speed_multiplier)

        self.overrender = QtWidgets.QDoubleSpinBox()
        self.overrender.setDecimals(3)
        self.overrender.setRange(0.1, 10.0)
        self.overrender.setSingleStep(0.1)
        self.overrender.setValue(2.0)
        form_render.addRow("Overrender (--overrender)", self.overrender)

        self.trail_alpha = QtWidgets.QDoubleSpinBox()
        self.trail_alpha.setDecimals(4)
        self.trail_alpha.setRange(0.0, 1.0)
        self.trail_alpha.setSingleStep(0.02)
        self.trail_alpha.setValue(0.0)
        form_render.addRow("Trail alpha (--trail_alpha)", self.trail_alpha)

        self.trail_blur = QtWidgets.QSpinBox()
        self.trail_blur.setRange(0, 100)
        self.trail_blur.setValue(0)
        form_render.addRow("Trail blur (--trail_blur)", self.trail_blur)

        self.trail_dim = QtWidgets.QSpinBox()
        self.trail_dim.setRange(0, 255)
        self.trail_dim.setValue(0)
        form_render.addRow("Trail dim (--trail_dim)", self.trail_dim)

        self.hitfx_scale_mul = QtWidgets.QDoubleSpinBox()
        self.hitfx_scale_mul.setDecimals(3)
        self.hitfx_scale_mul.setRange(0.1, 10.0)
        self.hitfx_scale_mul.setSingleStep(0.1)
        self.hitfx_scale_mul.setValue(1.0)
        form_render.addRow("HitFX scale mul (--hitfx_scale_mul)", self.hitfx_scale_mul)

        self.multicolor_lines = QtWidgets.QCheckBox("Multicolor lines (--multicolor_lines)")
        form_render.addRow("", self.multicolor_lines)

        self.note_outline = QtWidgets.QCheckBox("Note outline (--note_outline)")
        form_render.addRow("", self.note_outline)

        self.line_alpha_affects_notes = QtWidgets.QComboBox()
        self.line_alpha_affects_notes.addItems(["negative_only", "never", "always"])
        form_render.addRow("Line alpha affects notes (--line_alpha_affects_notes)", self.line_alpha_affects_notes)

        page_render_layout.addStretch(1)

        page_gameplay = QtWidgets.QWidget()
        self.toolbox.addItem(page_gameplay, "🎮 Gameplay")
        page_gameplay_layout = QtWidgets.QVBoxLayout(page_gameplay)
        try:
            page_gameplay_layout.setContentsMargins(12, 12, 12, 12)
            page_gameplay_layout.setSpacing(12)
        except Exception:
            pass

        self.grp_game = QtWidgets.QGroupBox("Gameplay")
        page_gameplay_layout.addWidget(self.grp_game)
        game_root = QtWidgets.QVBoxLayout(self.grp_game)
        try:
            game_root.setContentsMargins(8, 12, 8, 8)
            game_root.setSpacing(6)
        except Exception:
            pass

        self.autoplay = QtWidgets.QCheckBox("Enable autoplay (--autoplay)")
        game_root.addWidget(self.autoplay)

        self.no_cull = QtWidgets.QCheckBox("Disable culling (--no_cull)")
        game_root.addWidget(self.no_cull)

        self.no_cull_screen = QtWidgets.QCheckBox("Disable screen cull (--no_cull_screen)")
        game_root.addWidget(self.no_cull_screen)

        self.no_cull_enter_time = QtWidgets.QCheckBox("Disable time-window cull (--no_cull_enter_time)")
        game_root.addWidget(self.no_cull_enter_time)

        self.grp_hitsounds = QtWidgets.QGroupBox("Hitsounds")
        page_gameplay_layout.addWidget(self.grp_hitsounds)
        form_hits = QtWidgets.QFormLayout(self.grp_hitsounds)
        try:
            form_hits.setContentsMargins(8, 12, 8, 8)
            form_hits.setVerticalSpacing(8)
        except Exception:
            pass

        self.hitsound_min_interval_ms = QtWidgets.QSpinBox()
        self.hitsound_min_interval_ms.setRange(0, 1000)
        self.hitsound_min_interval_ms.setValue(30)
        form_hits.addRow("Min interval ms (--hitsound_min_interval_ms)", self.hitsound_min_interval_ms)

        self.grp_game_adv = QtWidgets.QGroupBox("Gameplay (Advanced)")
        self.grp_game_adv.setCheckable(True)
        self.grp_game_adv.setChecked(False)
        page_gameplay_layout.addWidget(self.grp_game_adv)
        _g2_outer = QtWidgets.QVBoxLayout(self.grp_game_adv)
        try:
            _g2_outer.setContentsMargins(8, 12, 8, 8)
            _g2_outer.setSpacing(8)
        except Exception:
            pass
        self.grp_game_adv_body = QtWidgets.QWidget()
        _g2_outer.addWidget(self.grp_game_adv_body)
        try:
            self.grp_game_adv.toggled.connect(self.grp_game_adv_body.setVisible)
            self.grp_game_adv_body.setVisible(False)
        except Exception:
            pass
        form_game2 = QtWidgets.QFormLayout(self.grp_game_adv_body)
        try:
            form_game2.setVerticalSpacing(8)
        except Exception:
            pass

        self.simulateplay = QtWidgets.QCheckBox("Simulate play (--simulateplay)")
        form_game2.addRow("", self.simulateplay)

        self.simulateplay_mode = QtWidgets.QComboBox()
        self.simulateplay_mode.addItems(["conservative", "aggressive", "extreme"])
        form_game2.addRow("Simulate mode (--simulateplay_mode)", self.simulateplay_mode)

        self.simulateplay_max_pointers = QtWidgets.QSpinBox()
        self.simulateplay_max_pointers.setRange(1, 10)
        self.simulateplay_max_pointers.setValue(2)
        form_game2.addRow("Max pointers (--simulateplay_max_pointers)", self.simulateplay_max_pointers)

        self.simulateplay_ipad = QtWidgets.QCheckBox("Route to iPad (--simulateplay_ipad)")
        form_game2.addRow("", self.simulateplay_ipad)

        self.ipad_bundle_id = QtWidgets.QLineEdit()
        form_game2.addRow("bundleId (--ipad_bundle_id)", self.ipad_bundle_id)

        self.ipad_udid = QtWidgets.QLineEdit()
        form_game2.addRow("UDID (--ipad_udid)", self.ipad_udid)

        self.ipad_device_name = QtWidgets.QLineEdit()
        self.ipad_device_name.setText("iPad")
        form_game2.addRow("Device name (--ipad_device_name)", self.ipad_device_name)

        self.ipad_appium_server = QtWidgets.QLineEdit()
        self.ipad_appium_server.setText("http://127.0.0.1:4723")
        form_game2.addRow("Appium server (--ipad_appium_server)", self.ipad_appium_server)

        self.ipad_mjpeg_url = QtWidgets.QLineEdit()
        form_game2.addRow("MJPEG url (--ipad_mjpeg_url)", self.ipad_mjpeg_url)

        self.ipad_move_hz = QtWidgets.QDoubleSpinBox()
        self.ipad_move_hz.setDecimals(2)
        self.ipad_move_hz.setRange(0.0, 240.0)
        self.ipad_move_hz.setValue(25.0)
        form_game2.addRow("Move Hz (--ipad_move_hz)", self.ipad_move_hz)

        self.ipad_preview_fps = QtWidgets.QDoubleSpinBox()
        self.ipad_preview_fps.setDecimals(2)
        self.ipad_preview_fps.setRange(0.0, 120.0)
        self.ipad_preview_fps.setValue(15.0)
        form_game2.addRow("Preview FPS (--ipad_preview_fps)", self.ipad_preview_fps)

        self.ipad_max_retries = QtWidgets.QSpinBox()
        self.ipad_max_retries.setRange(0, 50)
        self.ipad_max_retries.setValue(2)
        form_game2.addRow("Max retries (--ipad_max_retries)", self.ipad_max_retries)

        self.ipad_retry_backoff_s = QtWidgets.QDoubleSpinBox()
        self.ipad_retry_backoff_s.setDecimals(3)
        self.ipad_retry_backoff_s.setRange(0.0, 10.0)
        self.ipad_retry_backoff_s.setValue(0.35)
        form_game2.addRow("Retry backoff s (--ipad_retry_backoff_s)", self.ipad_retry_backoff_s)

        self.ipad_reconnect = QtWidgets.QCheckBox("Reconnect (--ipad_reconnect)")
        self.ipad_reconnect.setChecked(True)
        form_game2.addRow("", self.ipad_reconnect)

        self.ipad_activate_app = QtWidgets.QCheckBox("Activate app (--ipad_activate_app)")
        form_game2.addRow("", self.ipad_activate_app)

        self.ipad_viewport_x0 = QtWidgets.QDoubleSpinBox()
        self.ipad_viewport_x0.setDecimals(4)
        self.ipad_viewport_x0.setRange(0.0, 1.0)
        self.ipad_viewport_x0.setValue(0.0)
        form_game2.addRow("Viewport x0 (--ipad_viewport_x0)", self.ipad_viewport_x0)

        self.ipad_viewport_y0 = QtWidgets.QDoubleSpinBox()
        self.ipad_viewport_y0.setDecimals(4)
        self.ipad_viewport_y0.setRange(0.0, 1.0)
        self.ipad_viewport_y0.setValue(0.0)
        form_game2.addRow("Viewport y0 (--ipad_viewport_y0)", self.ipad_viewport_y0)

        self.ipad_viewport_x1 = QtWidgets.QDoubleSpinBox()
        self.ipad_viewport_x1.setDecimals(4)
        self.ipad_viewport_x1.setRange(0.0, 1.0)
        self.ipad_viewport_x1.setValue(1.0)
        form_game2.addRow("Viewport x1 (--ipad_viewport_x1)", self.ipad_viewport_x1)

        self.ipad_viewport_y1 = QtWidgets.QDoubleSpinBox()
        self.ipad_viewport_y1.setDecimals(4)
        self.ipad_viewport_y1.setRange(0.0, 1.0)
        self.ipad_viewport_y1.setValue(1.0)
        form_game2.addRow("Viewport y1 (--ipad_viewport_y1)", self.ipad_viewport_y1)

        self.judge_script = QtWidgets.QLineEdit()
        self.judge_script_browse = QtWidgets.QPushButton("Browse")
        self.judge_script_browse.clicked.connect(self._browse_judge_script)
        js_row = QtWidgets.QHBoxLayout()
        js_row.addWidget(self.judge_script)
        js_row.addWidget(self.judge_script_browse)
        form_game2.addRow("Judge script (--judge_script)", js_row)

        self.hold_fx_interval_ms = QtWidgets.QSpinBox()
        self.hold_fx_interval_ms.setRange(0, 5000)
        self.hold_fx_interval_ms.setValue(200)
        form_game2.addRow("Hold FX interval ms (--hold_fx_interval_ms)", self.hold_fx_interval_ms)

        self.hold_tail_tol = QtWidgets.QDoubleSpinBox()
        self.hold_tail_tol.setDecimals(4)
        self.hold_tail_tol.setRange(0.0, 5.0)
        self.hold_tail_tol.setValue(0.8)
        form_game2.addRow("Hold tail tol (--hold_tail_tol)", self.hold_tail_tol)

        self.judge_width = QtWidgets.QDoubleSpinBox()
        self.judge_width.setDecimals(4)
        self.judge_width.setRange(0.0, 1.0)
        self.judge_width.setValue(0.12)
        form_game2.addRow("Judge width (--judge_width)", self.judge_width)

        self.judge_height = QtWidgets.QDoubleSpinBox()
        self.judge_height.setDecimals(4)
        self.judge_height.setRange(0.0, 1.0)
        self.judge_height.setValue(0.06)
        form_game2.addRow("Judge height (--judge_height)", self.judge_height)

        self.flick_threshold = QtWidgets.QDoubleSpinBox()
        self.flick_threshold.setDecimals(4)
        self.flick_threshold.setRange(0.0, 1.0)
        self.flick_threshold.setValue(0.02)
        form_game2.addRow("Flick threshold (--flick_threshold)", self.flick_threshold)

        self.start_time = QtWidgets.QDoubleSpinBox()
        self.start_time.setDecimals(3)
        self.start_time.setRange(0.0, 99999.0)
        self.start_time.setValue(0.0)
        form_game2.addRow("Start time s (--start_time)", self.start_time)

        self.end_time = QtWidgets.QDoubleSpinBox()
        self.end_time.setDecimals(3)
        self.end_time.setRange(0.0, 99999.0)
        self.end_time.setValue(0.0)
        form_game2.addRow("End time s (--end_time)", self.end_time)

        page_gameplay_layout.addStretch(1)

        page_advanced = QtWidgets.QWidget()
        self.toolbox.addItem(page_advanced, "🔧 Advanced")
        page_advanced_layout = QtWidgets.QVBoxLayout(page_advanced)
        try:
            page_advanced_layout.setContentsMargins(12, 12, 12, 12)
            page_advanced_layout.setSpacing(12)
        except Exception:
            pass

        self.grp_ui = QtWidgets.QGroupBox("UI")
        self.grp_ui.setCheckable(True)
        self.grp_ui.setChecked(False)
        page_advanced_layout.addWidget(self.grp_ui)
        _ui_outer = QtWidgets.QVBoxLayout(self.grp_ui)
        try:
            _ui_outer.setContentsMargins(8, 12, 8, 8)
            _ui_outer.setSpacing(8)
        except Exception:
            pass
        self.grp_ui_body = QtWidgets.QWidget()
        _ui_outer.addWidget(self.grp_ui_body)
        try:
            self.grp_ui.toggled.connect(self.grp_ui_body.setVisible)
            self.grp_ui_body.setVisible(False)
        except Exception:
            pass
        form_ui = QtWidgets.QFormLayout(self.grp_ui_body)
        try:
            form_ui.setVerticalSpacing(8)
        except Exception:
            pass

        self.no_title_overlay = QtWidgets.QCheckBox("No title overlay (--no_title_overlay)")
        form_ui.addRow("", self.no_title_overlay)

        self.advance_seq_overlay = QtWidgets.QCheckBox("Advance seq overlay (--advance_seq_overlay)")
        form_ui.addRow("", self.advance_seq_overlay)

        self.font_path = QtWidgets.QLineEdit()
        self.font_browse = QtWidgets.QPushButton("Browse")
        self.font_browse.clicked.connect(self._browse_font)
        font_row = QtWidgets.QHBoxLayout()
        font_row.addWidget(self.font_path)
        font_row.addWidget(self.font_browse)
        form_ui.addRow("Font (--font_path)", font_row)

        self.font_size_multiplier = QtWidgets.QDoubleSpinBox()
        self.font_size_multiplier.setDecimals(3)
        self.font_size_multiplier.setRange(0.1, 10.0)
        self.font_size_multiplier.setValue(1.0)
        form_ui.addRow("Font size mul (--font_size_multiplier)", self.font_size_multiplier)

        self.advance_lazy_load = QtWidgets.QCheckBox("Advance lazy load (--advance_lazy_load)")
        form_ui.addRow("", self.advance_lazy_load)

        self.advance_lazy_cache = QtWidgets.QSpinBox()
        self.advance_lazy_cache.setRange(0, 999)
        self.advance_lazy_cache.setValue(1)
        form_ui.addRow("Lazy cache (--advance_lazy_cache)", self.advance_lazy_cache)

        self.advance_lazy_preload = QtWidgets.QCheckBox("Lazy preload (--advance_lazy_preload)")
        form_ui.addRow("", self.advance_lazy_preload)

        self.advance_lazy_scan_total_notes = QtWidgets.QCheckBox("Lazy scan total notes (--advance_lazy_scan_total_notes)")
        form_ui.addRow("", self.advance_lazy_scan_total_notes)

        self.grp_rpe = QtWidgets.QGroupBox("RPE")
        page_advanced_layout.addWidget(self.grp_rpe)
        form_rpe = QtWidgets.QFormLayout(self.grp_rpe)
        try:
            form_rpe.setContentsMargins(8, 12, 8, 8)
            form_rpe.setVerticalSpacing(8)
        except Exception:
            pass

        self.rpe_easing_shift = QtWidgets.QSpinBox()
        self.rpe_easing_shift.setRange(-10, 10)
        self.rpe_easing_shift.setValue(0)
        form_rpe.addRow("Easing shift (--rpe_easing_shift)", self.rpe_easing_shift)

        self.grp_debug = QtWidgets.QGroupBox("Debug")
        self.grp_debug.setCheckable(True)
        self.grp_debug.setChecked(False)
        page_advanced_layout.addWidget(self.grp_debug)
        _dbg_outer = QtWidgets.QVBoxLayout(self.grp_debug)
        try:
            _dbg_outer.setContentsMargins(8, 12, 8, 8)
            _dbg_outer.setSpacing(8)
        except Exception:
            pass
        self.grp_debug_body = QtWidgets.QWidget()
        _dbg_outer.addWidget(self.grp_debug_body)
        try:
            self.grp_debug.toggled.connect(self.grp_debug_body.setVisible)
            self.grp_debug_body.setVisible(False)
        except Exception:
            pass
        dbg_root = QtWidgets.QVBoxLayout(self.grp_debug_body)
        try:
            dbg_root.setSpacing(6)
        except Exception:
            pass

        self.basic_debug = QtWidgets.QCheckBox("Basic debug overlay (--basic_debug)")
        dbg_root.addWidget(self.basic_debug)

        self.debug_note_info = QtWidgets.QCheckBox("Debug note info (--debug_note_info)")
        dbg_root.addWidget(self.debug_note_info)

        self.debug_line_label = QtWidgets.QCheckBox("Debug line label (--debug_line_label)")
        dbg_root.addWidget(self.debug_line_label)

        self.debug_line_stats = QtWidgets.QCheckBox("Debug line stats (--debug_line_stats)")
        dbg_root.addWidget(self.debug_line_stats)

        self.debug_judge_windows = QtWidgets.QCheckBox("Debug judge windows (--debug_judge_windows)")
        dbg_root.addWidget(self.debug_judge_windows)

        self.debug_pointer = QtWidgets.QCheckBox("Debug pointer (--debug_pointer)")
        dbg_root.addWidget(self.debug_pointer)

        self.debug_particles = QtWidgets.QCheckBox("Debug particles (--debug_particles)")
        dbg_root.addWidget(self.debug_particles)

        self.hit_debug = QtWidgets.QCheckBox("Hit debug (--hit_debug)")
        dbg_root.addWidget(self.hit_debug)

        page_advanced_layout.addStretch(1)

        btn_row = QtWidgets.QHBoxLayout()
        launch_outer.addLayout(btn_row)

        self.btn_load_cfg = QtWidgets.QPushButton("Load Config")
        self.btn_load_cfg.clicked.connect(self._load_config)
        btn_row.addWidget(self.btn_load_cfg)

        self.btn_save_cfg = QtWidgets.QPushButton("Save Config")
        self.btn_save_cfg.clicked.connect(self._save_config)
        btn_row.addWidget(self.btn_save_cfg)

        self.btn_refresh = QtWidgets.QPushButton("Refresh")
        self.btn_refresh.clicked.connect(self._refresh_all)
        btn_row.addWidget(self.btn_refresh)

        self.btn_launch = QtWidgets.QPushButton("Launch")
        self.btn_launch.clicked.connect(self._launch)
        btn_row.addWidget(self.btn_launch)

        self.btn_export_pcc = QtWidgets.QPushButton("Export PCC")
        self.btn_export_pcc.clicked.connect(self._export_pcc)
        btn_row.addWidget(self.btn_export_pcc)

        self.status = QtWidgets.QLabel("")
        launch_outer.addWidget(self.status)

        preview_root = QtWidgets.QVBoxLayout(tab_preview)
        prev_btn_row = QtWidgets.QHBoxLayout()
        preview_root.addLayout(prev_btn_row)
        self.btn_preview = QtWidgets.QPushButton("Refresh Preview")
        self.btn_preview.clicked.connect(self._refresh_preview)
        prev_btn_row.addWidget(self.btn_preview)
        prev_btn_row.addStretch(1)

        self.preview_text = QtWidgets.QPlainTextEdit()
        self.preview_text.setReadOnly(True)
        try:
            self.preview_text.setFont(QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.SystemFont.FixedFont))
        except Exception:
            pass
        preview_root.addWidget(self.preview_text)

        for w in [
            self.input_path,
            self.advance_path,
            self.respack_path,
            self.bg_path,
            self.bgm_path,
            self.ipad_bundle_id,
            self.ipad_udid,
            self.ipad_device_name,
            self.ipad_appium_server,
            self.ipad_mjpeg_url,
            self.font_path,
            self.judge_script,
        ]:
            w.textChanged.connect(self._refresh_all_debounced)

        self.backend.currentIndexChanged.connect(self._refresh_all_debounced)
        self.audio_backend.currentIndexChanged.connect(self._refresh_all_debounced)
        self.w_spin.valueChanged.connect(self._refresh_all_debounced)
        self.h_spin.valueChanged.connect(self._refresh_all_debounced)
        self.expand_spin.valueChanged.connect(self._refresh_all_debounced)
        self.chart_speed.valueChanged.connect(self._refresh_all_debounced)
        self.approach.valueChanged.connect(self._refresh_all_debounced)
        self.note_scale_x.valueChanged.connect(self._refresh_all_debounced)
        self.note_scale_y.valueChanged.connect(self._refresh_all_debounced)
        self.note_flow_speed_multiplier.valueChanged.connect(self._refresh_all_debounced)
        self.overrender.valueChanged.connect(self._refresh_all_debounced)
        self.trail_alpha.valueChanged.connect(self._refresh_all_debounced)
        self.trail_blur.valueChanged.connect(self._refresh_all_debounced)
        self.trail_dim.valueChanged.connect(self._refresh_all_debounced)
        self.hitfx_scale_mul.valueChanged.connect(self._refresh_all_debounced)
        self.multicolor_lines.stateChanged.connect(self._refresh_all_debounced)
        self.note_outline.stateChanged.connect(self._refresh_all_debounced)
        self.line_alpha_affects_notes.currentIndexChanged.connect(self._refresh_all_debounced)
        self.bg_blur.valueChanged.connect(self._refresh_all_debounced)
        self.bg_dim.valueChanged.connect(self._refresh_all_debounced)
        self.force_bgm.stateChanged.connect(self._refresh_all_debounced)
        self.bgm_volume.valueChanged.connect(self._refresh_all_debounced)
        self.hitsound_min_interval_ms.valueChanged.connect(self._refresh_all_debounced)
        self.autoplay.stateChanged.connect(self._refresh_all_debounced)
        self.no_cull.stateChanged.connect(self._refresh_all_debounced)
        self.no_cull_screen.stateChanged.connect(self._refresh_all_debounced)
        self.no_cull_enter_time.stateChanged.connect(self._refresh_all_debounced)
        self.simulateplay.stateChanged.connect(self._refresh_all_debounced)
        self.simulateplay_mode.currentIndexChanged.connect(self._refresh_all_debounced)
        self.simulateplay_max_pointers.valueChanged.connect(self._refresh_all_debounced)
        self.simulateplay_ipad.stateChanged.connect(self._refresh_all_debounced)
        self.ipad_move_hz.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_preview_fps.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_max_retries.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_retry_backoff_s.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_reconnect.stateChanged.connect(self._refresh_all_debounced)
        self.ipad_activate_app.stateChanged.connect(self._refresh_all_debounced)
        self.ipad_viewport_x0.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_viewport_y0.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_viewport_x1.valueChanged.connect(self._refresh_all_debounced)
        self.ipad_viewport_y1.valueChanged.connect(self._refresh_all_debounced)
        self.hold_fx_interval_ms.valueChanged.connect(self._refresh_all_debounced)
        self.hold_tail_tol.valueChanged.connect(self._refresh_all_debounced)
        self.judge_width.valueChanged.connect(self._refresh_all_debounced)
        self.judge_height.valueChanged.connect(self._refresh_all_debounced)
        self.flick_threshold.valueChanged.connect(self._refresh_all_debounced)
        self.start_time.valueChanged.connect(self._refresh_all_debounced)
        self.end_time.valueChanged.connect(self._refresh_all_debounced)
        self.no_title_overlay.stateChanged.connect(self._refresh_all_debounced)
        self.advance_seq_overlay.stateChanged.connect(self._refresh_all_debounced)
        self.font_size_multiplier.valueChanged.connect(self._refresh_all_debounced)
        self.advance_lazy_load.stateChanged.connect(self._refresh_all_debounced)
        self.advance_lazy_cache.valueChanged.connect(self._refresh_all_debounced)
        self.advance_lazy_preload.stateChanged.connect(self._refresh_all_debounced)
        self.advance_lazy_scan_total_notes.stateChanged.connect(self._refresh_all_debounced)
        self.rpe_easing_shift.valueChanged.connect(self._refresh_all_debounced)
        self.debug_line_label.stateChanged.connect(self._refresh_all_debounced)
        self.debug_line_stats.stateChanged.connect(self._refresh_all_debounced)
        self.debug_judge_windows.stateChanged.connect(self._refresh_all_debounced)
        self.debug_pointer.stateChanged.connect(self._refresh_all_debounced)
        self.basic_debug.stateChanged.connect(self._refresh_all_debounced)
        self.debug_note_info.stateChanged.connect(self._refresh_all_debounced)
        self.debug_particles.stateChanged.connect(self._refresh_all_debounced)
        self.hit_debug.stateChanged.connect(self._refresh_all_debounced)
        self.lang.currentIndexChanged.connect(self._refresh_all_debounced)
        self.lang.currentIndexChanged.connect(self._apply_ui_language)
        self.quiet.stateChanged.connect(self._refresh_all_debounced)
        self.no_color.stateChanged.connect(self._refresh_all_debounced)

        self._apply_ui_language()
        self._apply_tooltips()
        self._refresh_all()

    def _ui_lang(self) -> str:
        try:
            v = str(self.lang.currentText()).strip()
        except Exception:
            v = ""
        if v == "zh-CN":
            return "zh-CN"
        return "en"

    def _tr_ui(self, en: str, zh_cn: str) -> str:
        return zh_cn if self._ui_lang() == "zh-CN" else en

    def _apply_ui_language(self) -> None:
        try:
            self.w.setWindowTitle(self._tr_ui("Mini Phigros Renderer Launcher", "Mini Phigros 渲染器 启动器"))
        except Exception:
            pass
        try:
            self.tabs.setTabText(0, self._tr_ui("Launch", "启动"))
            self.tabs.setTabText(1, self._tr_ui("Preview", "预览"))
        except Exception:
            pass

        try:
            self.grp_input.setTitle(self._tr_ui("Input", "输入"))
            self.grp_library.setTitle(self._tr_ui("Chart Library", "谱面库"))
            self.grp_cfg.setTitle(self._tr_ui("Config / CUI", "配置 / 控制台"))
            self.grp_assets.setTitle(self._tr_ui("Assets", "资源"))
            self.grp_audio.setTitle(self._tr_ui("Audio", "音频"))
            self.grp_backend.setTitle(self._tr_ui("Backend / Window", "后端 / 窗口"))
            self.grp_render.setTitle(self._tr_ui("Render", "渲染"))
            self.grp_game.setTitle(self._tr_ui("Gameplay", "玩法"))
            self.grp_hitsounds.setTitle(self._tr_ui("Hitsounds", "打击音"))
            self.grp_game_adv.setTitle(self._tr_ui("Gameplay (Advanced)", "玩法（高级）"))
            self.grp_ui.setTitle(self._tr_ui("UI", "界面"))
            self.grp_rpe.setTitle(self._tr_ui("RPE", "RPE"))
            self.grp_debug.setTitle(self._tr_ui("Debug", "调试"))
        except Exception:
            pass

        try:
            self.btn_load_cfg.setText(self._tr_ui("Load Config", "加载配置"))
            self.btn_save_cfg.setText(self._tr_ui("Save Config", "保存配置"))
            self.btn_refresh.setText(self._tr_ui("Refresh", "刷新"))
            self.btn_launch.setText(self._tr_ui("Launch", "启动"))
            self.btn_preview.setText(self._tr_ui("Refresh Preview", "刷新预览"))
            self.btn_export_pcc.setText(self._tr_ui("Export PCC", "导出 PCC"))
        except Exception:
            pass

        try:
            b = self._tr_ui("Browse", "浏览")
            self.input_browse.setText(b)
            self.advance_browse.setText(b)
            self.charts_dir_browse.setText(b)
            self.respack_browse.setText(b)
            self.bg_browse.setText(b)
            self.bgm_browse.setText(b)
            self.font_browse.setText(b)
            self.judge_script_browse.setText(b)
        except Exception:
            pass

        try:
            self.btn_scan_charts.setText(self._tr_ui("Scan", "扫描"))
        except Exception:
            pass

        self._update_chart_entry_texts()

        self._schedule_thumb_load()

        try:
            self.quiet.setText(self._tr_ui("Quiet (--quiet)", "安静模式 (--quiet)"))
            self.no_color.setText(self._tr_ui("No color (--no_color)", "禁用颜色 (--no_color)"))
            self.force_bgm.setText(self._tr_ui("Force BGM override (--force)", "强制使用 BGM 覆盖 (--force)"))
            self.multicolor_lines.setText(self._tr_ui("Multicolor lines (--multicolor_lines)", "多彩轨道线 (--multicolor_lines)"))
            self.note_outline.setText(self._tr_ui("Note outline (--note_outline)", "音符描边 (--note_outline)"))
            self.autoplay.setText(self._tr_ui("Enable autoplay (--autoplay)", "自动游玩 (--autoplay)"))
            self.no_cull.setText(self._tr_ui("Disable culling (--no_cull)", "禁用裁剪 (--no_cull)"))
            self.no_cull_screen.setText(self._tr_ui("Disable screen cull (--no_cull_screen)", "禁用屏幕裁剪 (--no_cull_screen)"))
            self.no_cull_enter_time.setText(self._tr_ui("Disable time-window cull (--no_cull_enter_time)", "禁用时间窗裁剪 (--no_cull_enter_time)"))
            self.simulateplay.setText(self._tr_ui("Simulate play (--simulateplay)", "模拟游玩 (--simulateplay)"))
            self.simulateplay_ipad.setText(self._tr_ui("Route to iPad (--simulateplay_ipad)", "路由到 iPad (--simulateplay_ipad)"))
            self.ipad_reconnect.setText(self._tr_ui("Reconnect (--ipad_reconnect)", "断线重连 (--ipad_reconnect)"))
            self.ipad_activate_app.setText(self._tr_ui("Activate app (--ipad_activate_app)", "激活应用 (--ipad_activate_app)"))
            self.no_title_overlay.setText(self._tr_ui("No title overlay (--no_title_overlay)", "禁用标题覆盖层 (--no_title_overlay)"))
            self.advance_seq_overlay.setText(self._tr_ui("Advance seq overlay (--advance_seq_overlay)", "显示序列覆盖层 (--advance_seq_overlay)"))
            self.advance_lazy_load.setText(self._tr_ui("Advance lazy load (--advance_lazy_load)", "Advance 懒加载 (--advance_lazy_load)"))
            self.advance_lazy_preload.setText(self._tr_ui("Lazy preload (--advance_lazy_preload)", "懒加载预加载 (--advance_lazy_preload)"))
            self.advance_lazy_scan_total_notes.setText(self._tr_ui("Lazy scan total notes (--advance_lazy_scan_total_notes)", "懒加载统计总 notes (--advance_lazy_scan_total_notes)"))
            self.basic_debug.setText(self._tr_ui("Basic debug overlay (--basic_debug)", "基础调试覆盖层 (--basic_debug)"))
            self.debug_note_info.setText(self._tr_ui("Debug note info (--debug_note_info)", "调试音符信息 (--debug_note_info)"))
            self.debug_line_label.setText(self._tr_ui("Debug line label (--debug_line_label)", "调试轨道标签 (--debug_line_label)"))
            self.debug_line_stats.setText(self._tr_ui("Debug line stats (--debug_line_stats)", "调试轨道统计 (--debug_line_stats)"))
            self.debug_judge_windows.setText(self._tr_ui("Debug judge windows (--debug_judge_windows)", "调试判定窗口 (--debug_judge_windows)"))
            self.debug_pointer.setText(self._tr_ui("Debug pointer (--debug_pointer)", "调试指针 (--debug_pointer)"))
            self.debug_particles.setText(self._tr_ui("Debug particles (--debug_particles)", "调试粒子 (--debug_particles)"))
            self.hit_debug.setText(self._tr_ui("Hit debug (--hit_debug)", "击打调试 (--hit_debug)"))
        except Exception:
            pass

        self._apply_tooltips()

    def _set_tt(self, w: Any, en: str, zh_cn: str) -> None:
        try:
            w.setToolTip(self._tr_ui(en, zh_cn))
        except Exception:
            pass

    def _apply_tooltips(self) -> None:
        # Input
        self._set_tt(self.input_path, "Chart json OR chart pack folder OR .zip/.pez pack.", "谱面 json / 谱面包文件夹 / .zip/.pez 谱面包")
        self._set_tt(self.advance_path, "Advance config JSON path.", "Advance 配置 JSON 路径")

        self._set_tt(self.charts_dir, "Directory to scan for charts (default: ./charts).", "扫描谱面目录（默认：./charts）")
        self._set_tt(self.btn_scan_charts, "Scan directory and list all playable entries.", "扫描目录并列出所有可用谱面")
        self._set_tt(self.chart_entries, "Click an entry to fill --input.", "点击条目自动填充 --input")

        # Config / CUI
        self._set_tt(self.lang, "Language for CLI/UI (zh-CN/en).", "语言（zh-CN/en），影响启动器与部分输出")
        self._set_tt(self.quiet, "Less console output.", "减少控制台输出")
        self._set_tt(self.no_color, "Disable ANSI colored logs.", "禁用彩色日志")

        # Assets
        self._set_tt(self.respack_path, "Respack zip path.", "资源包 zip 路径")
        self._set_tt(self.bg_path, "Override background image.", "覆盖背景图片")
        self._set_tt(self.bg_blur, "Background blur strength (downscale factor).", "背景模糊强度（缩放因子）")
        self._set_tt(self.bg_dim, "Background dim alpha 0..255.", "背景变暗透明度 0..255")

        # Audio
        self._set_tt(self.bgm_path, "Override BGM audio file (mp3/ogg/wav).", "覆盖 BGM 音频文件（mp3/ogg/wav）")
        self._set_tt(self.force_bgm, "Force using --bgm even if chart pack provides music.", "即使谱面包自带音乐也强制使用 --bgm")
        self._set_tt(self.bgm_volume, "BGM volume 0..1.", "BGM 音量 0..1")

        # Backend / Window
        self._set_tt(self.backend, "Render backend.", "渲染后端")
        self._set_tt(self.audio_backend, "Audio backend.", "音频后端")
        self._set_tt(self.w_spin, "Window width.", "窗口宽度")
        self._set_tt(self.h_spin, "Window height.", "窗口高度")
        self._set_tt(self.expand_spin, "Render expand factor.", "渲染扩展倍率")

        # Render
        self._set_tt(self.chart_speed, "Chart speed multiplier.", "谱面速度倍率")
        self._set_tt(self.approach, "Seconds ahead to draw.", "提前绘制秒数")
        self._set_tt(self.note_scale_x, "Note scale X.", "音符 X 缩放")
        self._set_tt(self.note_scale_y, "Note scale Y.", "音符 Y 缩放")
        self._set_tt(self.note_flow_speed_multiplier, "Extra flow speed multiplier.", "额外流速倍率")
        self._set_tt(self.overrender, "Overrender factor.", "过渲染倍率")
        self._set_tt(self.trail_alpha, "Trail alpha (0 disables).", "拖影透明度（0 关闭）")
        self._set_tt(self.trail_blur, "Trail blur strength.", "拖影模糊强度")
        self._set_tt(self.trail_dim, "Trail dim alpha.", "拖影变暗透明度")
        self._set_tt(self.hitfx_scale_mul, "Hit effect scale multiplier.", "打击特效缩放倍率")
        self._set_tt(self.multicolor_lines, "Enable multicolor judge lines.", "启用多彩判定线")
        self._set_tt(self.note_outline, "Enable note outline.", "启用音符描边")
        self._set_tt(self.line_alpha_affects_notes, "How line alpha affects note alpha.", "轨道线透明度如何影响音符")

        # Gameplay
        self._set_tt(self.autoplay, "Auto play.", "自动游玩")
        self._set_tt(self.no_cull, "Disable note culling (slower).", "禁用裁剪（更慢）")
        self._set_tt(self.no_cull_screen, "Disable screen-space culling.", "禁用屏幕裁剪")
        self._set_tt(self.no_cull_enter_time, "Disable time-window culling.", "禁用时间窗裁剪")

        # Hitsounds
        self._set_tt(self.hitsound_min_interval_ms, "Min hitsound interval in ms.", "打击音最小间隔（毫秒）")

        # Gameplay advanced
        self._set_tt(self.simulateplay, "Simulate touch inputs.", "模拟触控输入")
        self._set_tt(self.simulateplay_mode, "Simulateplay mode.", "模拟游玩模式")
        self._set_tt(self.simulateplay_max_pointers, "Max pointers used by simulateplay.", "模拟游玩最大触点数")
        self._set_tt(self.simulateplay_ipad, "Send gestures to a real iPad via Appium.", "通过 Appium 把手势发送到真实 iPad")
        self._set_tt(self.ipad_bundle_id, "iOS app bundleId (required for iPad mode).", "iOS 应用 bundleId（iPad 模式必填）")
        self._set_tt(self.ipad_udid, "Target device UDID (optional).", "目标设备 UDID（可选）")
        self._set_tt(self.ipad_device_name, "Device name used by Appium.", "Appium 使用的设备名")
        self._set_tt(self.ipad_appium_server, "Appium server URL.", "Appium 服务地址")
        self._set_tt(self.ipad_mjpeg_url, "Optional MJPEG preview stream URL.", "可选 MJPEG 低延迟预览流地址")
        self._set_tt(self.ipad_move_hz, "Max move events per second.", "每秒最多 move 事件数")
        self._set_tt(self.ipad_preview_fps, "iPad screenshot preview FPS (0 disables).", "iPad 截图预览 FPS（0 关闭）")
        self._set_tt(self.ipad_max_retries, "Max retries on failure.", "失败最大重试次数")
        self._set_tt(self.ipad_retry_backoff_s, "Retry backoff seconds.", "重试退避秒数")
        self._set_tt(self.ipad_reconnect, "Reconnect Appium session on failure.", "失败时重连 Appium")
        self._set_tt(self.ipad_activate_app, "Activate the app after connect.", "连接后激活应用")
        self._set_tt(self.ipad_viewport_x0, "Viewport mapping x0 (0..1).", "映射视口 x0（0..1）")
        self._set_tt(self.ipad_viewport_y0, "Viewport mapping y0 (0..1).", "映射视口 y0（0..1）")
        self._set_tt(self.ipad_viewport_x1, "Viewport mapping x1 (0..1).", "映射视口 x1（0..1）")
        self._set_tt(self.ipad_viewport_y1, "Viewport mapping y1 (0..1).", "映射视口 y1（0..1）")
        self._set_tt(self.judge_script, "Optional judge script JSON.", "可选判定脚本 JSON")
        self._set_tt(self.hold_fx_interval_ms, "Hold FX spawn interval in ms.", "长按特效间隔（毫秒）")
        self._set_tt(self.hold_tail_tol, "Hold tail tolerance.", "长按尾部容差")
        self._set_tt(self.judge_width, "Judgement width ratio.", "判定宽度比例")
        self._set_tt(self.judge_height, "Judgement height ratio.", "判定高度比例")
        self._set_tt(self.flick_threshold, "Flick threshold ratio.", "滑动阈值比例")
        self._set_tt(self.start_time, "Start time in seconds (0 disables).", "开始时间（秒，0 关闭）")
        self._set_tt(self.end_time, "End time in seconds (0 disables).", "结束时间（秒，0 关闭）")

        # UI
        self._set_tt(self.no_title_overlay, "Disable title overlay.", "禁用标题覆盖层")
        self._set_tt(self.advance_seq_overlay, "Show advance sequence overlay.", "显示序列覆盖层")
        self._set_tt(self.font_path, "Override font file.", "覆盖字体文件")
        self._set_tt(self.font_size_multiplier, "Font size multiplier.", "字体大小倍率")
        self._set_tt(self.advance_lazy_load, "Lazy-load advance items.", "Advance 懒加载")
        self._set_tt(self.advance_lazy_cache, "Lazy cache size.", "懒加载缓存大小")
        self._set_tt(self.advance_lazy_preload, "Preload lazy items.", "懒加载预加载")
        self._set_tt(self.advance_lazy_scan_total_notes, "Scan total notes in lazy mode.", "懒加载下统计总 notes")

        # RPE
        self._set_tt(self.rpe_easing_shift, "Shift RPE easingType index.", "RPE easingType 索引偏移")

        # Debug
        self._set_tt(self.basic_debug, "Basic debug overlay.", "基础调试覆盖层")
        self._set_tt(self.debug_note_info, "Show note debug info.", "显示音符调试信息")
        self._set_tt(self.debug_line_label, "Show line labels.", "显示轨道标签")
        self._set_tt(self.debug_line_stats, "Show line stats.", "显示轨道统计")
        self._set_tt(self.debug_judge_windows, "Show judge windows.", "显示判定窗口")
        self._set_tt(self.debug_pointer, "Show pointer overlay.", "显示触点调试")
        self._set_tt(self.debug_particles, "Debug particles.", "粒子调试")
        self._set_tt(self.hit_debug, "Hit debug details.", "击打调试详情")

    def show(self) -> None:
        self.w.show()

    def _browse_input(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select chart/json or pack")
        if p:
            self.input_path.setText(p)

    def _browse_charts_dir(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p = QtWidgets.QFileDialog.getExistingDirectory(self.w, "Select charts directory")
        if p:
            self.charts_dir.setText(p)
            self._scan_charts_dir()

    def _qt_user_role(self) -> int:
        try:
            return int(self.qt.QtCore.Qt.ItemDataRole.UserRole)  # type: ignore
        except Exception:
            return 256

    def _qt_user_role_meta(self) -> int:
        return int(self._qt_user_role()) + 1

    def _qt_user_role_thumb(self) -> int:
        return int(self._qt_user_role()) + 2

    def _qt_user_role_thumb_done(self) -> int:
        return int(self._qt_user_role()) + 3

    def _schedule_thumb_load(self) -> None:
        try:
            if self._thumb_pending:
                self._thumb_debounce.start()
        except Exception:
            pass

    def _thumb_load_tick(self) -> None:
        try:
            if not self._thumb_pending:
                return
        except Exception:
            return

        try:
            viewport = self.chart_entries.viewport()
            rect = viewport.rect()
            top_item = self.chart_entries.itemAt(self.qt.QtCore.QPoint(2, 2))
            bot_item = self.chart_entries.itemAt(self.qt.QtCore.QPoint(2, max(2, int(rect.height()) - 2)))
            top_row = self.chart_entries.row(top_item) if top_item is not None else 0
            bot_row = self.chart_entries.row(bot_item) if bot_item is not None else (self.chart_entries.count() - 1)
        except Exception:
            top_row, bot_row = 0, -1

        batch: list[Any] = []
        try:
            if bot_row >= top_row and self.chart_entries.count() > 0:
                for r in range(int(top_row), int(bot_row) + 1):
                    it = self.chart_entries.item(r)
                    if it is None:
                        continue
                    if self._thumb_item_needs_load(it):
                        batch.append(it)
                        if len(batch) >= 3:
                            break
        except Exception:
            batch = []

        if len(batch) < 3:
            try:
                for it in list(self._thumb_pending):
                    if it in batch:
                        continue
                    if self._thumb_item_needs_load(it):
                        batch.append(it)
                        if len(batch) >= 3:
                            break
            except Exception:
                pass

        if not batch:
            try:
                self._thumb_pending = [it for it in self._thumb_pending if self._thumb_item_needs_load(it)]
            except Exception:
                pass
            return

        for it in batch:
            try:
                self._thumb_load_one(it)
            except Exception:
                pass

        try:
            self._thumb_pending = [it for it in self._thumb_pending if self._thumb_item_needs_load(it)]
        except Exception:
            pass

        self._schedule_thumb_load()

    def _thumb_item_needs_load(self, item: Any) -> bool:
        role_thumb = self._qt_user_role_thumb()
        role_done = self._qt_user_role_thumb_done()
        try:
            done = bool(item.data(role_done))
        except Exception:
            done = False
        if done:
            return False
        try:
            src = item.data(role_thumb)
        except Exception:
            src = None
        return bool(src)

    def _thumb_load_one(self, item: Any) -> None:
        role_thumb = self._qt_user_role_thumb()
        role_done = self._qt_user_role_thumb_done()
        try:
            src = item.data(role_thumb)
        except Exception:
            src = None
        if not src:
            try:
                item.setData(role_done, True)
            except Exception:
                pass
            return

        pm = None
        try:
            kind = str(getattr(src, "get", lambda _k, _d=None: None)("kind", "") or "").strip().lower()
            if kind == "zip":
                z = getattr(src, "get", lambda _k, _d=None: None)("zip")
                inner = getattr(src, "get", lambda _k, _d=None: None)("inner")
                if z and inner:
                    pm = self._make_thumb_from_zip(str(z), str(inner))
            elif kind == "path":
                p = getattr(src, "get", lambda _k, _d=None: None)("path")
                if p:
                    pm = self._make_thumb_from_path(str(p))
        except Exception:
            pm = None

        if pm is not None:
            try:
                widget = self.chart_entries.itemWidget(item)
                if widget and hasattr(widget, "_chart_card"):
                    widget._chart_card.set_background(pm)
            except Exception:
                pass
        try:
            item.setData(role_done, True)
        except Exception:
            pass

    def _default_charts_dir(self) -> str:
        try:
            if os.path.isdir("charts"):
                return os.path.abspath("charts")
        except Exception:
            pass

        try:
            repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
            cand = os.path.join(repo_root, "charts")
            if os.path.isdir(cand):
                return os.path.abspath(cand)
        except Exception:
            pass

        try:
            return os.path.abspath(os.getcwd())
        except Exception:
            return ""

    def _load_pack_info_quick(self, p: str) -> dict:
        p = os.path.abspath(str(p))
        if os.path.isdir(p):
            info_p = os.path.join(p, "info.yml")
            if os.path.exists(info_p):
                try:
                    with open(info_p, "r", encoding="utf-8") as f:
                        return dict(_parse_info_yml_minimal(f.read()) or {})
                except Exception:
                    return {}
            return {}

        if os.path.isfile(p) and p.lower().endswith((".zip", ".pez")):
            try:
                with zipfile.ZipFile(p, "r") as z:
                    for name in z.namelist():
                        if str(name).replace("\\", "/").rstrip("/") == "info.yml":
                            try:
                                raw = z.read(name)
                                try:
                                    text = raw.decode("utf-8")
                                except Exception:
                                    text = raw.decode("utf-8", errors="ignore")
                                return dict(_parse_info_yml_minimal(text) or {})
                            except Exception:
                                return {}
            except Exception:
                return {}
        return {}

    def _format_entry_label(self, p: str) -> str:
        p2 = os.path.abspath(str(p))
        base = os.path.basename(p2)
        try:
            if os.path.isfile(p2) and p2.lower().endswith((".json", ".pec", ".pe", ".pcc")):
                stem = os.path.splitext(os.path.basename(p2))[0].strip().upper()
                parent = os.path.basename(os.path.dirname(os.path.abspath(p2)))
                if stem in {"EZ", "HD", "IN", "AT", "SP"}:
                    return f"{parent} [{stem}]"
        except Exception:
            pass
        try:
            if os.path.isdir(p2) and os.path.exists(os.path.join(p2, "info.yml")):
                info = self._load_pack_info_quick(p2)
                name = info.get("name") or info.get("title") or info.get("song")
                level = info.get("level")
                if name and level:
                    return f"{name} [{level}]"
                if name:
                    return str(name)
        except Exception:
            pass
        try:
            if os.path.isfile(p2) and p2.lower().endswith((".zip", ".pez")):
                info = self._load_pack_info_quick(p2)
                name = info.get("name") or info.get("title") or info.get("song")
                level = info.get("level")
                if name and level:
                    return f"{name} [{level}]"
                if name:
                    return str(name)
        except Exception:
            pass
        return base

    def _format_entry_text_from_meta(self, meta: dict) -> str:
        title = str(meta.get("title") or "")
        bg_label = str(meta.get("bg_label") or "")
        bgm_label = str(meta.get("bgm_label") or "")
        lines = [title] if title else []

        if bgm_label:
            lines.append(self._tr_ui(f"BGM: {bgm_label}", f"BGM：{bgm_label}"))
        if bg_label:
            lines.append(self._tr_ui(f"BG: {bg_label}", f"曲绘：{bg_label}"))
        return "\n".join(lines) if lines else ""

    def _update_chart_entry_texts(self) -> None:
        role_meta = self._qt_user_role_meta()
        try:
            n = int(self.chart_entries.count())
        except Exception:
            n = 0
        for i in range(n):
            try:
                it = self.chart_entries.item(i)
            except Exception:
                it = None
            if it is None:
                continue
            try:
                meta = it.data(role_meta)
            except Exception:
                meta = None
            if not isinstance(meta, dict):
                continue
            try:
                it.setText(self._format_entry_text_from_meta(meta))
            except Exception:
                pass

    def _make_thumb_from_path(self, p: str) -> Optional[Any]:
        p = os.path.abspath(str(p))
        if not os.path.exists(p):
            return None
        try:
            if os.path.getsize(p) > 15 * 1024 * 1024:
                return None
        except Exception:
            pass
        try:
            reader = self.qt.QtGui.QImageReader(str(p))
            try:
                reader.setAutoTransform(True)
            except Exception:
                pass
            try:
                w, h = int(self._thumb_size[0]), int(self._thumb_size[1])
                reader.setScaledSize(self.qt.QtCore.QSize(w, h))
            except Exception:
                pass
            img = reader.read()
            if img is None or getattr(img, "isNull", lambda: True)():
                return None
            return self.qt.QtGui.QPixmap.fromImage(img)
        except Exception:
            return None

    def _make_thumb_from_zip(self, zip_path: str, inner_path: str) -> Optional[Any]:
        zip_path = os.path.abspath(str(zip_path))
        inner_path = str(inner_path).replace("\\", "/").lstrip("/")
        if not os.path.exists(zip_path):
            return None
        try:
            with zipfile.ZipFile(zip_path, "r") as z:
                try:
                    info = z.getinfo(inner_path)
                except Exception:
                    info = None
                if info is not None:
                    try:
                        if int(getattr(info, "file_size", 0) or 0) > 15 * 1024 * 1024:
                            return None
                    except Exception:
                        pass
                raw = z.read(inner_path)
        except Exception:
            return None
        try:
            ba = self.qt.QtCore.QByteArray(raw)
            buf = self.qt.QtCore.QBuffer(ba)
            buf.open(self.qt.QtCore.QIODevice.OpenModeFlag.ReadOnly)
            reader = self.qt.QtGui.QImageReader(buf)
            try:
                reader.setAutoTransform(True)
            except Exception:
                pass
            try:
                w, h = int(self._thumb_size[0]), int(self._thumb_size[1])
                reader.setScaledSize(self.qt.QtCore.QSize(w, h))
            except Exception:
                pass
            img = reader.read()
            try:
                buf.close()
            except Exception:
                pass
            if img is None or getattr(img, "isNull", lambda: True)():
                return None
            return self.qt.QtGui.QPixmap.fromImage(img)
        except Exception:
            return None

    def _library_meta_for_input(self, input_path: str) -> tuple[dict, Optional[dict]]:
        p = os.path.abspath(str(input_path))
        title = self._format_entry_label(p)
        bg_label = ""
        bgm_label = ""
        thumb_src = None

        if os.path.isfile(p) and p.lower().endswith((".zip", ".pez")):
            info = self._load_pack_info_quick(p)
            bg_fn = str(info.get("illustration", "") or "").strip()
            bgm_fn = str(info.get("music", "") or "").strip()
            if bg_fn:
                bg_label = bg_fn
                thumb_src = {"kind": "zip", "zip": p, "inner": bg_fn}
            if bgm_fn:
                bgm_label = bgm_fn
        elif os.path.isdir(p) and os.path.exists(os.path.join(p, "info.yml")):
            info = self._load_pack_info_quick(p)
            bg_fn = str(info.get("illustration", "") or "").strip()
            bgm_fn = str(info.get("music", "") or "").strip()
            if bg_fn:
                bg_label = bg_fn
                thumb_src = {"kind": "path", "path": os.path.join(p, bg_fn)}
            if bgm_fn:
                bgm_label = bgm_fn
        else:
            try:
                _chart_path, music_path, bg_path, _info, _keepalive = _resolve_pack_or_chart(str(p))
            except Exception:
                music_path, bg_path = None, None
            if music_path:
                try:
                    bgm_label = os.path.basename(str(music_path))
                except Exception:
                    bgm_label = str(music_path)
            if bg_path:
                try:
                    bg_label = os.path.basename(str(bg_path))
                except Exception:
                    bg_label = str(bg_path)
                try:
                    thumb_src = {"kind": "path", "path": str(bg_path)}
                except Exception:
                    thumb_src = None

        meta = {
            "title": title,
            "bg_label": bg_label,
            "bgm_label": bgm_label,
        }
        return meta, thumb_src

    def _scan_charts_dir(self) -> None:
        QtWidgets = self.qt.QtWidgets
        try:
            self._thumb_load_gen += 1
            self._thumb_pending = []
        except Exception:
            pass
        try:
            self.chart_entries.clear()
        except Exception:
            pass

        d = ""
        try:
            d = self.charts_dir.text().strip()
        except Exception:
            d = ""
        if not d:
            d = self._default_charts_dir()
            try:
                self.charts_dir.setText(d)
            except Exception:
                pass

        if not d or (not os.path.isdir(d)):
            try:
                self.status.setText(self._tr_ui("Charts dir not found.", "未找到谱面目录"))
            except Exception:
                pass
            return

        try:
            inputs = discover_chart_inputs(str(d))
        except Exception:
            inputs = []

        role = self._qt_user_role()
        role_meta = self._qt_user_role_meta()
        role_thumb = self._qt_user_role_thumb()
        role_done = self._qt_user_role_thumb_done()
        for p in list(inputs or []):
            try:
                meta, thumb_src = self._library_meta_for_input(str(p))
                item = QtWidgets.QListWidgetItem()
                item.setData(role, os.path.abspath(str(p)))
                item.setData(role_meta, dict(meta))
                try:
                    item.setData(role_thumb, thumb_src)
                except Exception:
                    pass
                try:
                    item.setData(role_done, False)
                except Exception:
                    pass
                try:
                    item.setToolTip(os.path.abspath(str(p)))
                except Exception:
                    pass
                try:
                    item.setSizeHint(self.qt.QtCore.QSize(630, 165))
                except Exception:
                    pass
                
                card = ChartCardWidget(
                    self.qt,
                    title=str(meta.get("title", "")),
                    bg_label=str(meta.get("bg_label", "")),
                    bgm_label=str(meta.get("bgm_label", "")),
                    width=620,
                    height=160
                )
                card.widget._chart_card = card
                
                self.chart_entries.addItem(item)
                try:
                    self.chart_entries.setItemWidget(item, card.widget)
                except Exception:
                    pass
                
                try:
                    if thumb_src:
                        self._thumb_pending.append(item)
                except Exception:
                    pass
            except Exception:
                continue

        try:
            self.status.setText(self._tr_ui(f"Found: {len(inputs)}", f"找到：{len(inputs)}"))
        except Exception:
            pass

        self._update_chart_entry_texts()

    def _on_chart_entry_selected(self) -> None:
        try:
            items = self.chart_entries.selectedItems()
        except Exception:
            items = []
        if not items:
            return

        role = self._qt_user_role()
        try:
            p = str(items[0].data(role) or "").strip()
        except Exception:
            p = ""
        if not p:
            return

        try:
            self.input_path.setText(p)
        except Exception:
            pass

        try:
            adv_now = self.advance_path.text().strip()
        except Exception:
            adv_now = ""
        if not adv_now:
            try:
                base_dir = p if os.path.isdir(p) else os.path.dirname(os.path.abspath(p))
                cand = os.path.join(base_dir, "advance.json")
                if os.path.exists(cand):
                    self.advance_path.setText(cand)
            except Exception:
                pass

        # Auto-fill BGM and BG if they are currently empty
        try:
            bgm_now = self.bgm_path.text().strip()
        except Exception:
            bgm_now = ""
        if not bgm_now:
            try:
                _chart_path, music_path, bg_path, _info, _keepalive = _resolve_pack_or_chart(str(p))
                if music_path:
                    self.bgm_path.setText(str(music_path))
            except Exception:
                pass

        try:
            bg_now = self.bg_path.text().strip()
        except Exception:
            bg_now = ""
        if not bg_now:
            try:
                _chart_path, music_path, bg_path, _info, _keepalive = _resolve_pack_or_chart(str(p))
                if bg_path:
                    self.bg_path.setText(str(bg_path))
            except Exception:
                pass

        self._refresh_all_debounced()

    def _browse_advance(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select advance JSON")
        if p:
            self.advance_path.setText(p)

    def _browse_respack(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select respack zip")
        if p:
            self.respack_path.setText(p)

    def _browse_bg(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select background image")
        if p:
            self.bg_path.setText(p)

    def _browse_bgm(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select BGM audio")
        if p:
            self.bgm_path.setText(p)

    def _browse_font(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select font")
        if p:
            self.font_path.setText(p)

    def _browse_judge_script(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Select judge script JSON")
        if p:
            self.judge_script.setText(p)

    def _make_args_obj_for_config(self) -> Any:
        from ..app import build_arg_parser

        ap = build_arg_parser()
        args = ap.parse_args([])
        try:
            setattr(args, "lang", self.lang.currentText().strip() or None)
        except Exception:
            pass
        try:
            setattr(args, "quiet", bool(self.quiet.isChecked()))
        except Exception:
            pass
        try:
            setattr(args, "no_color", bool(self.no_color.isChecked()))
        except Exception:
            pass
        setattr(args, "backend", self.backend.currentText())
        setattr(args, "audio_backend", self.audio_backend.currentText())
        setattr(args, "w", int(self.w_spin.value()))
        setattr(args, "h", int(self.h_spin.value()))
        setattr(args, "expand", float(self.expand_spin.value()))
        setattr(args, "chart_speed", float(self.chart_speed.value()))
        setattr(args, "approach", float(self.approach.value()))
        setattr(args, "note_scale_x", float(self.note_scale_x.value()))
        setattr(args, "note_scale_y", float(self.note_scale_y.value()))
        setattr(args, "note_flow_speed_multiplier", float(self.note_flow_speed_multiplier.value()))
        setattr(args, "overrender", float(self.overrender.value()))
        setattr(args, "trail_alpha", float(self.trail_alpha.value()))
        setattr(args, "trail_blur", int(self.trail_blur.value()))
        setattr(args, "trail_dim", int(self.trail_dim.value()))
        setattr(args, "hitfx_scale_mul", float(self.hitfx_scale_mul.value()))
        setattr(args, "multicolor_lines", bool(self.multicolor_lines.isChecked()))
        setattr(args, "note_outline", bool(self.note_outline.isChecked()))
        setattr(args, "line_alpha_affects_notes", str(self.line_alpha_affects_notes.currentText()))
        setattr(args, "respack", self.respack_path.text().strip() or None)
        setattr(args, "bg", self.bg_path.text().strip() or None)
        setattr(args, "bg_blur", int(self.bg_blur.value()))
        setattr(args, "bg_dim", int(self.bg_dim.value()))
        setattr(args, "bgm", self.bgm_path.text().strip() or None)
        setattr(args, "force", bool(self.force_bgm.isChecked()))
        setattr(args, "bgm_volume", float(self.bgm_volume.value()))
        setattr(args, "hitsound_min_interval_ms", int(self.hitsound_min_interval_ms.value()))
        setattr(args, "autoplay", bool(self.autoplay.isChecked()))
        setattr(args, "no_cull", bool(self.no_cull.isChecked()))
        setattr(args, "no_cull_screen", bool(self.no_cull_screen.isChecked()))
        setattr(args, "no_cull_enter_time", bool(self.no_cull_enter_time.isChecked()))
        setattr(args, "simulateplay", bool(self.simulateplay.isChecked()))
        setattr(args, "simulateplay_mode", str(self.simulateplay_mode.currentText()))
        setattr(args, "simulateplay_max_pointers", int(self.simulateplay_max_pointers.value()))
        setattr(args, "simulateplay_ipad", bool(self.simulateplay_ipad.isChecked()))
        setattr(args, "ipad_bundle_id", self.ipad_bundle_id.text().strip() or None)
        setattr(args, "ipad_udid", self.ipad_udid.text().strip() or None)
        setattr(args, "ipad_device_name", self.ipad_device_name.text().strip() or "iPad")
        setattr(args, "ipad_appium_server", self.ipad_appium_server.text().strip() or "http://127.0.0.1:4723")
        setattr(args, "ipad_mjpeg_url", self.ipad_mjpeg_url.text().strip() or None)
        setattr(args, "ipad_move_hz", float(self.ipad_move_hz.value()))
        setattr(args, "ipad_preview_fps", float(self.ipad_preview_fps.value()))
        setattr(args, "ipad_max_retries", int(self.ipad_max_retries.value()))
        setattr(args, "ipad_retry_backoff_s", float(self.ipad_retry_backoff_s.value()))
        setattr(args, "ipad_reconnect", bool(self.ipad_reconnect.isChecked()))
        setattr(args, "ipad_activate_app", bool(self.ipad_activate_app.isChecked()))
        setattr(args, "ipad_viewport_x0", float(self.ipad_viewport_x0.value()))
        setattr(args, "ipad_viewport_y0", float(self.ipad_viewport_y0.value()))
        setattr(args, "ipad_viewport_x1", float(self.ipad_viewport_x1.value()))
        setattr(args, "ipad_viewport_y1", float(self.ipad_viewport_y1.value()))
        setattr(args, "judge_script", self.judge_script.text().strip() or None)
        setattr(args, "hold_fx_interval_ms", int(self.hold_fx_interval_ms.value()))
        setattr(args, "hold_tail_tol", float(self.hold_tail_tol.value()))
        setattr(args, "judge_width", float(self.judge_width.value()))
        setattr(args, "judge_height", float(self.judge_height.value()))
        setattr(args, "flick_threshold", float(self.flick_threshold.value()))
        try:
            setattr(args, "start_time", None if float(self.start_time.value()) <= 1e-9 else float(self.start_time.value()))
        except Exception:
            pass
        try:
            setattr(args, "end_time", None if float(self.end_time.value()) <= 1e-9 else float(self.end_time.value()))
        except Exception:
            pass
        setattr(args, "no_title_overlay", bool(self.no_title_overlay.isChecked()))
        setattr(args, "advance_seq_overlay", bool(self.advance_seq_overlay.isChecked()))
        setattr(args, "font_path", self.font_path.text().strip() or None)
        setattr(args, "font_size_multiplier", float(self.font_size_multiplier.value()))
        setattr(args, "advance_lazy_load", bool(self.advance_lazy_load.isChecked()))
        setattr(args, "advance_lazy_cache", int(self.advance_lazy_cache.value()))
        setattr(args, "advance_lazy_preload", bool(self.advance_lazy_preload.isChecked()))
        setattr(args, "advance_lazy_scan_total_notes", bool(self.advance_lazy_scan_total_notes.isChecked()))
        setattr(args, "rpe_easing_shift", int(self.rpe_easing_shift.value()))
        setattr(args, "basic_debug", bool(self.basic_debug.isChecked()))
        setattr(args, "debug_note_info", bool(self.debug_note_info.isChecked()))
        setattr(args, "debug_line_label", bool(self.debug_line_label.isChecked()))
        setattr(args, "debug_line_stats", bool(self.debug_line_stats.isChecked()))
        setattr(args, "debug_judge_windows", bool(self.debug_judge_windows.isChecked()))
        setattr(args, "debug_pointer", bool(self.debug_pointer.isChecked()))
        setattr(args, "debug_particles", bool(self.debug_particles.isChecked()))
        setattr(args, "hit_debug", bool(self.hit_debug.isChecked()))
        return args

    def _load_config(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getOpenFileName(self.w, "Load config (v2 JSON)")
        if not p:
            return
        try:
            cfg_v2_raw = load_config_v2(str(p))
            flat_cfg, _mods_cfg = flatten_config_v2(cfg_v2_raw)
            if isinstance(flat_cfg, dict):
                for k, v in flat_cfg.items():
                    try:
                        if k == "backend" and isinstance(v, str):
                            self.backend.setCurrentText(v)
                        elif k == "audio_backend" and isinstance(v, str):
                            self.audio_backend.setCurrentText(v)
                        elif k == "lang" and isinstance(v, str):
                            self.lang.setCurrentText(v)
                        elif k == "w":
                            self.w_spin.setValue(int(v))
                        elif k == "h":
                            self.h_spin.setValue(int(v))
                        elif k == "expand":
                            self.expand_spin.setValue(float(v))
                        elif k == "chart_speed":
                            self.chart_speed.setValue(float(v))
                        elif k == "approach":
                            self.approach.setValue(float(v))
                        elif k == "note_scale_x":
                            self.note_scale_x.setValue(float(v))
                        elif k == "note_scale_y":
                            self.note_scale_y.setValue(float(v))
                        elif k == "note_flow_speed_multiplier":
                            self.note_flow_speed_multiplier.setValue(float(v))
                        elif k == "overrender":
                            self.overrender.setValue(float(v))
                        elif k == "trail_alpha":
                            self.trail_alpha.setValue(float(v))
                        elif k == "trail_blur":
                            self.trail_blur.setValue(int(v))
                        elif k == "trail_dim":
                            self.trail_dim.setValue(int(v))
                        elif k == "hitfx_scale_mul":
                            self.hitfx_scale_mul.setValue(float(v))
                        elif k == "multicolor_lines":
                            self.multicolor_lines.setChecked(bool(v))
                        elif k == "note_outline":
                            self.note_outline.setChecked(bool(v))
                        elif k == "no_note_outline":
                            self.note_outline.setChecked(not bool(v))
                        elif k == "line_alpha_affects_notes" and isinstance(v, str):
                            self.line_alpha_affects_notes.setCurrentText(v)
                        elif k == "respack":
                            self.respack_path.setText(str(v) if v else "")
                        elif k == "bg":
                            self.bg_path.setText(str(v) if v else "")
                        elif k == "bg_blur":
                            self.bg_blur.setValue(int(v))
                        elif k == "bg_dim":
                            self.bg_dim.setValue(int(v))
                        elif k == "bgm":
                            self.bgm_path.setText(str(v) if v else "")
                        elif k == "force":
                            self.force_bgm.setChecked(bool(v))
                        elif k == "bgm_volume":
                            self.bgm_volume.setValue(float(v))
                        elif k == "hitsound_min_interval_ms":
                            self.hitsound_min_interval_ms.setValue(int(v))
                        elif k == "autoplay":
                            self.autoplay.setChecked(bool(v))
                        elif k == "no_cull":
                            self.no_cull.setChecked(bool(v))
                        elif k == "no_cull_screen":
                            self.no_cull_screen.setChecked(bool(v))
                        elif k == "no_cull_enter_time":
                            self.no_cull_enter_time.setChecked(bool(v))
                        elif k == "simulateplay":
                            self.simulateplay.setChecked(bool(v))
                        elif k == "simulateplay_mode" and isinstance(v, str):
                            self.simulateplay_mode.setCurrentText(v)
                        elif k == "simulateplay_max_pointers":
                            self.simulateplay_max_pointers.setValue(int(v))
                        elif k == "simulateplay_ipad":
                            self.simulateplay_ipad.setChecked(bool(v))
                        elif k == "ipad_bundle_id":
                            self.ipad_bundle_id.setText(str(v) if v else "")
                        elif k == "ipad_udid":
                            self.ipad_udid.setText(str(v) if v else "")
                        elif k == "ipad_device_name":
                            self.ipad_device_name.setText(str(v) if v else "")
                        elif k == "ipad_appium_server":
                            self.ipad_appium_server.setText(str(v) if v else "")
                        elif k == "ipad_mjpeg_url":
                            self.ipad_mjpeg_url.setText(str(v) if v else "")
                        elif k == "ipad_move_hz":
                            self.ipad_move_hz.setValue(float(v))
                        elif k == "ipad_preview_fps":
                            self.ipad_preview_fps.setValue(float(v))
                        elif k == "ipad_max_retries":
                            self.ipad_max_retries.setValue(int(v))
                        elif k == "ipad_retry_backoff_s":
                            self.ipad_retry_backoff_s.setValue(float(v))
                        elif k == "ipad_reconnect":
                            self.ipad_reconnect.setChecked(bool(v))
                        elif k == "ipad_activate_app":
                            self.ipad_activate_app.setChecked(bool(v))
                        elif k == "ipad_viewport_x0":
                            self.ipad_viewport_x0.setValue(float(v))
                        elif k == "ipad_viewport_y0":
                            self.ipad_viewport_y0.setValue(float(v))
                        elif k == "ipad_viewport_x1":
                            self.ipad_viewport_x1.setValue(float(v))
                        elif k == "ipad_viewport_y1":
                            self.ipad_viewport_y1.setValue(float(v))
                        elif k == "judge_script":
                            self.judge_script.setText(str(v) if v else "")
                        elif k == "hold_fx_interval_ms":
                            self.hold_fx_interval_ms.setValue(int(v))
                        elif k == "hold_tail_tol":
                            self.hold_tail_tol.setValue(float(v))
                        elif k == "judge_width":
                            self.judge_width.setValue(float(v))
                        elif k == "judge_height":
                            self.judge_height.setValue(float(v))
                        elif k == "flick_threshold":
                            self.flick_threshold.setValue(float(v))
                        elif k == "start_time":
                            self.start_time.setValue(float(v) if v is not None else 0.0)
                        elif k == "end_time":
                            self.end_time.setValue(float(v) if v is not None else 0.0)
                        elif k == "no_title_overlay":
                            self.no_title_overlay.setChecked(bool(v))
                        elif k == "font_path":
                            self.font_path.setText(str(v) if v else "")
                        elif k == "font_size_multiplier":
                            self.font_size_multiplier.setValue(float(v))
                        elif k == "advance_lazy_load":
                            self.advance_lazy_load.setChecked(bool(v))
                        elif k == "advance_lazy_cache":
                            self.advance_lazy_cache.setValue(int(v))
                        elif k == "advance_lazy_preload":
                            self.advance_lazy_preload.setChecked(bool(v))
                        elif k == "advance_lazy_scan_total_notes":
                            self.advance_lazy_scan_total_notes.setChecked(bool(v))
                        elif k == "rpe_easing_shift":
                            self.rpe_easing_shift.setValue(int(v))
                        elif k == "basic_debug":
                            self.basic_debug.setChecked(bool(v))
                        elif k == "debug_line_label":
                            self.debug_line_label.setChecked(bool(v))
                        elif k == "debug_line_stats":
                            self.debug_line_stats.setChecked(bool(v))
                        elif k == "debug_judge_windows":
                            self.debug_judge_windows.setChecked(bool(v))
                        elif k == "debug_pointer":
                            self.debug_pointer.setChecked(bool(v))
                        elif k == "debug_note_info":
                            self.debug_note_info.setChecked(bool(v))
                        elif k == "debug_particles":
                            self.debug_particles.setChecked(bool(v))
                        elif k == "hit_debug":
                            self.hit_debug.setChecked(bool(v))
                    except Exception:
                        pass

            try:
                self.status.setText("Config loaded")
            except Exception:
                pass
        except Exception as e:
            try:
                self.status.setText(f"Config load failed: {e}")
            except Exception:
                pass

    def _export_pcc(self) -> None:
        QtWidgets = self.qt.QtWidgets
        in_p = self.input_path.text().strip()
        adv_p = self.advance_path.text().strip()
        if not in_p:
            try:
                self.status.setText("Export PCC failed: input is empty")
            except Exception:
                pass
            return
        if adv_p:
            try:
                self.status.setText("Export PCC failed: advance mode not supported")
            except Exception:
                pass
            return

        out_p, _ = QtWidgets.QFileDialog.getSaveFileName(self.w, "Export PCC", filter="PCC (*.pcc)")
        if not out_p:
            return

        pw, ok = QtWidgets.QInputDialog.getText(self.w, "PCC Password (optional)", "Password (leave empty for no encryption):", QtWidgets.QLineEdit.EchoMode.Password)
        if not ok:
            return
        pw = str(pw)
        pw = pw if pw.strip() else ""

        W = int(self.w_spin.value())
        H = int(self.h_spin.value())
        try:
            from ..pcc.exporter import export_input_to_pcc

            fmt_i, off_i, lc, nc = export_input_to_pcc(in_p, out_p, W=W, H=H, password=pw or None, compress=True)
            try:
                self.status.setText(f"Exported PCC: fmt={fmt_i} offset={off_i:.4f} lines={lc} notes={nc}")
            except Exception:
                pass
        except Exception as e:
            try:
                self.status.setText(f"Export PCC failed: {e}")
            except Exception:
                pass

        self._refresh_all()

    def _save_config(self) -> None:
        QtWidgets = self.qt.QtWidgets
        p, _ = QtWidgets.QFileDialog.getSaveFileName(self.w, "Save config (v2 JSON)")
        if not p:
            return
        try:
            args = self._make_args_obj_for_config()
            txt = dump_config_v2(args)
            with open(str(p), "w", encoding="utf-8") as f:
                f.write(txt)
            try:
                self.status.setText("Config saved")
            except Exception:
                pass
        except Exception as e:
            try:
                self.status.setText(f"Config save failed: {e}")
            except Exception:
                pass

    def _build_tokens(self) -> list[str]:
        in_p = self.input_path.text().strip()
        adv_p = self.advance_path.text().strip()
        rp_p = self.respack_path.text().strip()
        bg_p = self.bg_path.text().strip()
        bgm_p = self.bgm_path.text().strip()

        lang = self.lang.currentText().strip()
        if lang:
            tokens0: list[str] = ["--lang", lang]
        else:
            tokens0 = []
        if self.quiet.isChecked():
            tokens0 += ["--quiet"]
        if self.no_color.isChecked():
            tokens0 += ["--no_color"]

        if in_p:
            tokens = ["--input", in_p]
        if adv_p:
            tokens = ["--advance", adv_p]
        if (not in_p) and (not adv_p):
            tokens = []
        if in_p and adv_p:
            tokens = ["--input", in_p, "--advance", adv_p]

        if not tokens:
            return []

        tokens = list(tokens0) + list(tokens)

        tokens += ["--backend", self.backend.currentText()]
        tokens += ["--audio_backend", self.audio_backend.currentText()]
        tokens += ["--w", str(int(self.w_spin.value()))]
        tokens += ["--h", str(int(self.h_spin.value()))]
        tokens += ["--expand", str(float(self.expand_spin.value()))]

        tokens += ["--chart_speed", str(float(self.chart_speed.value()))]
        tokens += ["--approach", str(float(self.approach.value()))]
        tokens += ["--note_scale_x", str(float(self.note_scale_x.value()))]
        tokens += ["--note_scale_y", str(float(self.note_scale_y.value()))]

        if abs(float(self.note_flow_speed_multiplier.value()) - 1.0) > 1e-9:
            tokens += ["--note_flow_speed_multiplier", str(float(self.note_flow_speed_multiplier.value()))]
        if abs(float(self.overrender.value()) - 2.0) > 1e-9:
            tokens += ["--overrender", str(float(self.overrender.value()))]
        if float(self.trail_alpha.value()) > 1e-9:
            tokens += ["--trail_alpha", str(float(self.trail_alpha.value()))]
        if int(self.trail_blur.value()) != 0:
            tokens += ["--trail_blur", str(int(self.trail_blur.value()))]
        if int(self.trail_dim.value()) != 0:
            tokens += ["--trail_dim", str(int(self.trail_dim.value()))]
        if abs(float(self.hitfx_scale_mul.value()) - 1.0) > 1e-9:
            tokens += ["--hitfx_scale_mul", str(float(self.hitfx_scale_mul.value()))]
        if self.multicolor_lines.isChecked():
            tokens += ["--multicolor_lines"]
        if self.note_outline.isChecked():
            tokens += ["--note_outline"]
        if str(self.line_alpha_affects_notes.currentText()) != "negative_only":
            tokens += ["--line_alpha_affects_notes", str(self.line_alpha_affects_notes.currentText())]

        if rp_p:
            tokens += ["--respack", rp_p]

        if bg_p:
            tokens += ["--bg", bg_p]
        if int(self.bg_blur.value()) != 10:
            tokens += ["--bg_blur", str(int(self.bg_blur.value()))]
        if int(self.bg_dim.value()) != 120:
            tokens += ["--bg_dim", str(int(self.bg_dim.value()))]

        if bgm_p:
            tokens += ["--bgm", bgm_p]
        if self.force_bgm.isChecked():
            tokens += ["--force"]
        if abs(float(self.bgm_volume.value()) - 0.8) > 1e-9:
            tokens += ["--bgm_volume", str(float(self.bgm_volume.value()))]

        if self.autoplay.isChecked():
            tokens += ["--autoplay"]
        if self.no_cull.isChecked():
            tokens += ["--no_cull"]
        if self.no_cull_screen.isChecked():
            tokens += ["--no_cull_screen"]
        if self.no_cull_enter_time.isChecked():
            tokens += ["--no_cull_enter_time"]
        if self.basic_debug.isChecked():
            tokens += ["--basic_debug"]
        if self.debug_note_info.isChecked():
            tokens += ["--debug_note_info"]

        if int(self.hitsound_min_interval_ms.value()) != 30:
            tokens += ["--hitsound_min_interval_ms", str(int(self.hitsound_min_interval_ms.value()))]

        if self.simulateplay.isChecked():
            tokens += ["--simulateplay"]
        if str(self.simulateplay_mode.currentText()) != "conservative":
            tokens += ["--simulateplay_mode", str(self.simulateplay_mode.currentText())]
        if int(self.simulateplay_max_pointers.value()) != 2:
            tokens += ["--simulateplay_max_pointers", str(int(self.simulateplay_max_pointers.value()))]
        if self.simulateplay_ipad.isChecked():
            tokens += ["--simulateplay_ipad"]
        if self.ipad_bundle_id.text().strip():
            tokens += ["--ipad_bundle_id", self.ipad_bundle_id.text().strip()]
        if self.ipad_udid.text().strip():
            tokens += ["--ipad_udid", self.ipad_udid.text().strip()]
        if self.ipad_device_name.text().strip() and self.ipad_device_name.text().strip() != "iPad":
            tokens += ["--ipad_device_name", self.ipad_device_name.text().strip()]
        if self.ipad_appium_server.text().strip() and self.ipad_appium_server.text().strip() != "http://127.0.0.1:4723":
            tokens += ["--ipad_appium_server", self.ipad_appium_server.text().strip()]
        if self.ipad_mjpeg_url.text().strip():
            tokens += ["--ipad_mjpeg_url", self.ipad_mjpeg_url.text().strip()]
        if abs(float(self.ipad_move_hz.value()) - 25.0) > 1e-9:
            tokens += ["--ipad_move_hz", str(float(self.ipad_move_hz.value()))]
        if abs(float(self.ipad_preview_fps.value()) - 15.0) > 1e-9:
            tokens += ["--ipad_preview_fps", str(float(self.ipad_preview_fps.value()))]
        if int(self.ipad_max_retries.value()) != 2:
            tokens += ["--ipad_max_retries", str(int(self.ipad_max_retries.value()))]
        if abs(float(self.ipad_retry_backoff_s.value()) - 0.35) > 1e-9:
            tokens += ["--ipad_retry_backoff_s", str(float(self.ipad_retry_backoff_s.value()))]
        if not self.ipad_reconnect.isChecked():
            # default is True
            pass
        else:
            tokens += ["--ipad_reconnect"]
        if self.ipad_activate_app.isChecked():
            tokens += ["--ipad_activate_app"]
        if abs(float(self.ipad_viewport_x0.value()) - 0.0) > 1e-9:
            tokens += ["--ipad_viewport_x0", str(float(self.ipad_viewport_x0.value()))]
        if abs(float(self.ipad_viewport_y0.value()) - 0.0) > 1e-9:
            tokens += ["--ipad_viewport_y0", str(float(self.ipad_viewport_y0.value()))]
        if abs(float(self.ipad_viewport_x1.value()) - 1.0) > 1e-9:
            tokens += ["--ipad_viewport_x1", str(float(self.ipad_viewport_x1.value()))]
        if abs(float(self.ipad_viewport_y1.value()) - 1.0) > 1e-9:
            tokens += ["--ipad_viewport_y1", str(float(self.ipad_viewport_y1.value()))]
        if self.judge_script.text().strip():
            tokens += ["--judge_script", self.judge_script.text().strip()]

        if int(self.hold_fx_interval_ms.value()) != 200:
            tokens += ["--hold_fx_interval_ms", str(int(self.hold_fx_interval_ms.value()))]
        if abs(float(self.hold_tail_tol.value()) - 0.8) > 1e-9:
            tokens += ["--hold_tail_tol", str(float(self.hold_tail_tol.value()))]
        if abs(float(self.judge_width.value()) - 0.12) > 1e-9:
            tokens += ["--judge_width", str(float(self.judge_width.value()))]
        if abs(float(self.judge_height.value()) - 0.06) > 1e-9:
            tokens += ["--judge_height", str(float(self.judge_height.value()))]
        if abs(float(self.flick_threshold.value()) - 0.02) > 1e-9:
            tokens += ["--flick_threshold", str(float(self.flick_threshold.value()))]

        if float(self.start_time.value()) > 1e-9:
            tokens += ["--start_time", str(float(self.start_time.value()))]
        if float(self.end_time.value()) > 1e-9:
            tokens += ["--end_time", str(float(self.end_time.value()))]

        if self.no_title_overlay.isChecked():
            tokens += ["--no_title_overlay"]
        if self.advance_seq_overlay.isChecked():
            tokens += ["--advance_seq_overlay"]
        if self.font_path.text().strip():
            tokens += ["--font_path", self.font_path.text().strip()]
        if abs(float(self.font_size_multiplier.value()) - 1.0) > 1e-9:
            tokens += ["--font_size_multiplier", str(float(self.font_size_multiplier.value()))]
        if self.advance_lazy_load.isChecked():
            tokens += ["--advance_lazy_load"]
        if int(self.advance_lazy_cache.value()) != 1:
            tokens += ["--advance_lazy_cache", str(int(self.advance_lazy_cache.value()))]
        if self.advance_lazy_preload.isChecked():
            tokens += ["--advance_lazy_preload"]
        if self.advance_lazy_scan_total_notes.isChecked():
            tokens += ["--advance_lazy_scan_total_notes"]

        if int(self.rpe_easing_shift.value()) != 0:
            tokens += ["--rpe_easing_shift", str(int(self.rpe_easing_shift.value()))]

        if self.debug_line_label.isChecked():
            tokens += ["--debug_line_label"]
        if self.debug_line_stats.isChecked():
            tokens += ["--debug_line_stats"]
        if self.debug_judge_windows.isChecked():
            tokens += ["--debug_judge_windows"]
        if self.debug_pointer.isChecked():
            tokens += ["--debug_pointer"]
        if self.debug_particles.isChecked():
            tokens += ["--debug_particles"]
        if self.hit_debug.isChecked():
            tokens += ["--hit_debug"]

        return tokens

    def _refresh_all_debounced(self) -> None:
        try:
            self._preview_debounce.start()
        except Exception:
            pass
        self._refresh_validation_only()

    def _refresh_all(self) -> None:
        self._refresh_validation_only()
        self._refresh_preview()

    def _refresh_validation_only(self) -> None:
        in_p = self.input_path.text().strip()
        adv_p = self.advance_path.text().strip()

        if (not in_p) and (not adv_p):
            self.status.setText("Pick either --input or --advance")
            self.btn_launch.setEnabled(False)
            return
        if in_p and adv_p:
            self.status.setText("Provide only one of --input or --advance")
            self.btn_launch.setEnabled(False)
            return

        self.status.setText("")
        self.btn_launch.setEnabled(True)

    def _read_zip_text(self, zip_path: str, inner: str) -> Optional[str]:
        try:
            with zipfile.ZipFile(zip_path, "r") as z:
                data = z.read(inner)
            return data.decode("utf-8", errors="replace")
        except Exception:
            return None

    def _refresh_preview(self) -> None:
        W = int(self.w_spin.value())
        H = int(self.h_spin.value())
        in_p = self.input_path.text().strip()
        adv_p = self.advance_path.text().strip()
        rp_p = self.respack_path.text().strip()
        bg_override = self.bg_path.text().strip() or None
        bgm_override = self.bgm_path.text().strip() or None
        force_bgm = bool(self.force_bgm.isChecked())

        out: list[str] = []
        out.append(self._tr_ui("Input", "输入"))
        out.append(f"  input={in_p or '(none)'}")
        out.append(f"  advance={adv_p or '(none)'}")

        if in_p and (not adv_p):
            try:
                pack_bg: Optional[str] = None
                pack_music: Optional[str] = None
                if os.path.isdir(in_p) and os.path.exists(os.path.join(in_p, "info.yml")):
                    info_p = os.path.join(in_p, "info.yml")
                    with open(info_p, "r", encoding="utf-8") as f:
                        info = _parse_info_yml_minimal(f.read())
                    out.append(self._tr_ui("  chart_pack=folder", "  谱面包=文件夹"))
                    out.append(f"  pack.name={info.get('name', '')}")
                    out.append(f"  pack.level={info.get('level', '')}")
                    chart_fn = str(info.get('chart', 'chart.json'))
                    music_fn = str(info.get('music', 'song.mp3'))
                    bg_fn = str(info.get('illustration', 'background.png'))
                    pack_music = os.path.join(in_p, music_fn)
                    pack_bg = os.path.join(in_p, bg_fn)
                    chart_path = os.path.join(in_p, chart_fn)
                    out.append(f"  chart={chart_fn}  exists={os.path.exists(chart_path)}")
                    out.append(f"  music={music_fn}  exists={os.path.exists(os.path.join(in_p, music_fn))}")
                    out.append(f"  bg={bg_fn}  exists={os.path.exists(os.path.join(in_p, bg_fn))}")
                    try:
                        fmt, off, lines, notes = load_chart(chart_path, W, H)
                        out.append(f"  parsed.fmt={fmt}  offset={off:.4f}")
                        out.append(f"  parsed.lines={len(lines)}  notes={len(notes)}")
                        kind_counts: dict[str, int] = {}
                        for n in (notes or []):
                            try:
                                k = str(int(getattr(n, 'kind', -1)))
                            except Exception:
                                k = "?"
                            kind_counts[k] = int(kind_counts.get(k, 0)) + 1
                        if kind_counts:
                            out.append("  notes.by_kind=" + ", ".join(f"{k}:{v}" for (k, v) in sorted(kind_counts.items())))
                    except Exception as e:
                        out.append(f"  parsed.error={e}")
                elif os.path.isfile(in_p) and str(in_p).lower().endswith((".zip", ".pez")):
                    out.append(self._tr_ui("  chart_pack=zip", "  谱面包=压缩包"))
                    txt = self._read_zip_text(in_p, "info.yml")
                    if txt is None:
                        out.append("  pack.error=info.yml not found/readable")
                    else:
                        info = _parse_info_yml_minimal(txt)
                        out.append(f"  pack.name={info.get('name', '')}")
                        out.append(f"  pack.level={info.get('level', '')}")
                        chart_fn = str(info.get('chart', 'chart.json'))
                        music_fn = str(info.get('music', 'song.mp3'))
                        bg_fn = str(info.get('illustration', 'background.png'))
                        pack_music = music_fn
                        pack_bg = bg_fn
                        try:
                            with zipfile.ZipFile(in_p, "r") as z:
                                names = set(z.namelist())
                            out.append(f"  chart={chart_fn}  in_zip={chart_fn in names}")
                            out.append(f"  music={music_fn}  in_zip={music_fn in names}")
                            out.append(f"  bg={bg_fn}  in_zip={bg_fn in names}")
                        except Exception as e:
                            out.append(f"  pack.error={e}")

                        try:
                            with tempfile.TemporaryDirectory() as td:
                                chart_txt = self._read_zip_text(in_p, chart_fn)
                                if chart_txt is not None:
                                    p = os.path.join(td, os.path.basename(chart_fn))
                                    with open(p, "w", encoding="utf-8") as f:
                                        f.write(chart_txt)
                                    fmt, off, lines, notes = load_chart(p, W, H)
                                    out.append(f"  parsed.fmt={fmt}  offset={off:.4f}")
                                    out.append(f"  parsed.lines={len(lines)}  notes={len(notes)}")
                        except Exception as e:
                            out.append(f"  parsed.error={e}")
                else:
                    fmt, off, lines, notes = load_chart(in_p, W, H)
                    out.append(self._tr_ui("  chart=single", "  谱面=单文件"))
                    out.append(f"  parsed.fmt={fmt}  offset={off:.4f}")
                    out.append(f"  parsed.lines={len(lines)}  notes={len(notes)}")
                    kind_counts: dict[str, int] = {}
                    for n in (notes or []):
                        try:
                            k = str(int(getattr(n, 'kind', -1)))
                        except Exception:
                            k = "?"
                        kind_counts[k] = int(kind_counts.get(k, 0)) + 1
                    if kind_counts:
                        out.append("  notes.by_kind=" + ", ".join(f"{k}:{v}" for (k, v) in sorted(kind_counts.items())))

                out.append("")
                out.append(self._tr_ui("Effective Assets", "生效资源"))
                if bg_override:
                    out.append(f"  bg.override={bg_override}  exists={os.path.exists(bg_override)}")
                else:
                    out.append("  bg.override=(none)")
                if bgm_override:
                    out.append(f"  bgm.override={bgm_override}  exists={os.path.exists(bgm_override)}")
                else:
                    out.append("  bgm.override=(none)")

                eff_bg: Optional[str] = None
                if bg_override:
                    eff_bg = bg_override
                elif pack_bg:
                    eff_bg = pack_bg

                eff_bgm: Optional[str] = None
                if bgm_override and (force_bgm or (not pack_music)):
                    eff_bgm = bgm_override
                elif pack_music:
                    eff_bgm = pack_music
                elif bgm_override:
                    eff_bgm = bgm_override

                out.append(f"  bg.effective={eff_bg or '(none)'}")
                out.append(f"  bgm.effective={eff_bgm or '(none)'}")
                out.append(f"  bg_blur={int(self.bg_blur.value())}")
                out.append(f"  bg_dim={int(self.bg_dim.value())}")
                out.append(f"  bgm_volume={float(self.bgm_volume.value()):.3f}  force={force_bgm}")

                out.append("")
                out.append(self._tr_ui("Effective Settings", "生效设置"))
                out.append(f"  window={int(self.w_spin.value())}x{int(self.h_spin.value())}  expand={float(self.expand_spin.value()):.3f}")
                out.append(f"  backend={self.backend.currentText()}  audio_backend={self.audio_backend.currentText()}")
                out.append(f"  chart_speed={float(self.chart_speed.value()):.3f}  approach={float(self.approach.value()):.3f}")
                out.append(f"  note_scale={float(self.note_scale_x.value()):.3f},{float(self.note_scale_y.value()):.3f}  flow_mul={float(self.note_flow_speed_multiplier.value()):.3f}")
                out.append(f"  overrender={float(self.overrender.value()):.3f}  trail_alpha={float(self.trail_alpha.value()):.4f}  trail_blur={int(self.trail_blur.value())}  trail_dim={int(self.trail_dim.value())}")
                out.append(f"  hitfx_scale_mul={float(self.hitfx_scale_mul.value()):.3f}  multicolor_lines={bool(self.multicolor_lines.isChecked())}  note_outline={bool(self.note_outline.isChecked())}")
                out.append(f"  line_alpha_affects_notes={self.line_alpha_affects_notes.currentText()}")
                out.append(f"  cull: no_cull={bool(self.no_cull.isChecked())}  no_cull_screen={bool(self.no_cull_screen.isChecked())}  no_cull_enter_time={bool(self.no_cull_enter_time.isChecked())}")
                out.append(f"  autoplay={bool(self.autoplay.isChecked())}  simulateplay={bool(self.simulateplay.isChecked())}  mode={self.simulateplay_mode.currentText()}  max_pointers={int(self.simulateplay_max_pointers.value())}")
                out.append(f"  hitsound_min_interval_ms={int(self.hitsound_min_interval_ms.value())}")
                out.append(f"  judge: width={float(self.judge_width.value()):.4f} height={float(self.judge_height.value()):.4f} flick={float(self.flick_threshold.value()):.4f} hold_fx_interval_ms={int(self.hold_fx_interval_ms.value())} hold_tail_tol={float(self.hold_tail_tol.value()):.4f}")
                st = float(self.start_time.value())
                et = float(self.end_time.value())
                out.append(f"  trim: start_time={st:.3f} end_time={et:.3f}")
                out.append(f"  ui: no_title_overlay={bool(self.no_title_overlay.isChecked())} advance_seq_overlay={bool(self.advance_seq_overlay.isChecked())} font={(self.font_path.text().strip() or '(default)')} size_mul={float(self.font_size_multiplier.value()):.3f}")
                out.append(f"  advance_lazy: load={bool(self.advance_lazy_load.isChecked())} cache={int(self.advance_lazy_cache.value())} preload={bool(self.advance_lazy_preload.isChecked())} scan_total_notes={bool(self.advance_lazy_scan_total_notes.isChecked())}")
                out.append(f"  rpe_easing_shift={int(self.rpe_easing_shift.value())}")
                out.append(f"  debug: basic_debug={bool(self.basic_debug.isChecked())} note_info={bool(self.debug_note_info.isChecked())} line_label={bool(self.debug_line_label.isChecked())} line_stats={bool(self.debug_line_stats.isChecked())} judge_windows={bool(self.debug_judge_windows.isChecked())} pointer={bool(self.debug_pointer.isChecked())} particles={bool(self.debug_particles.isChecked())} hit_debug={bool(self.hit_debug.isChecked())}")
            except Exception as e:
                out.append(f"  error={e}")

        if adv_p:
            out.append("")
            out.append(self._tr_ui("Advance", "高级配置"))
            try:
                with open(adv_p, "r", encoding="utf-8") as f:
                    cfg = json.load(f) or {}
                out.append(f"  mode={cfg.get('mode', 'sequence')}")
                if cfg.get("mode", "sequence") == "tracks":
                    tracks = list(cfg.get("tracks", []) or [])
                    out.append(f"  tracks={len(tracks)}")
                    out.append(f"  main={cfg.get('main', 0)}")
                else:
                    items = list(cfg.get("items", []) or [])
                    out.append(f"  items={len(items)}")
                if cfg.get("mix", False):
                    out.append("  mix=true")
            except Exception as e:
                out.append(f"  error={e}")

        out.append("")
        out.append(self._tr_ui("Respack", "资源包"))
        out.append(f"  respack={rp_p or '(none)'}")
        if rp_p:
            try:
                txt = self._read_zip_text(rp_p, "info.yml")
                if txt is None:
                    out.append("  error=info.yml not found/readable")
                else:
                    info = _parse_info_yml_minimal(txt)
                    out.append(f"  hitFx={info.get('hitFx', '')}")
                    out.append(f"  hitFxDuration={info.get('hitFxDuration', '')}")
                    out.append(f"  hitFxScale={info.get('hitFxScale', '')}")
                    out.append(f"  hitFxRotate={info.get('hitFxRotate', '')}")
                    out.append(f"  hitFxTinted={info.get('hitFxTinted', '')}")
                    out.append(f"  holdAtlas={info.get('holdAtlas', '')}")
                    out.append(f"  holdAtlasMH={info.get('holdAtlasMH', '')}")
                    out.append(f"  holdRepeat={info.get('holdRepeat', '')}")
                    out.append(f"  holdCompact={info.get('holdCompact', '')}")
                    out.append(f"  holdKeepHead={info.get('holdKeepHead', '')}")
                    out.append(f"  holdTailNoScale={info.get('holdTailNoScale', '')}")
                    out.append(f"  hideParticles={info.get('hideParticles', '')}")
                    try:
                        with zipfile.ZipFile(rp_p, "r") as z:
                            names = set(z.namelist())
                        required = [
                            "click.png",
                            "drag.png",
                            "hold.png",
                            "flick.png",
                            "click_mh.png",
                            "drag_mh.png",
                            "hold_mh.png",
                            "flick_mh.png",
                        ]
                        missing = [n for n in required if n not in names]
                        out.append(f"  required_textures_missing={len(missing)}")
                        for n in missing[:12]:
                            out.append(f"    - {n}")
                        pngs = [n for n in names if n.lower().endswith('.png')]
                        oggs = [n for n in names if n.lower().endswith('.ogg')]
                        out.append(f"  png_files={len(pngs)}")
                        out.append(f"  ogg_files={len(oggs)}")
                    except Exception as e:
                        out.append(f"  error={e}")
            except Exception as e:
                out.append(f"  error={e}")

        self.preview_text.setPlainText("\n".join(out))

    def _launch(self) -> None:
        tokens = self._build_tokens()
        if not tokens:
            self.status.setText("Pick either --input or --advance")
            return
        in_p = self.input_path.text().strip()
        adv_p = self.advance_path.text().strip()
        if in_p and adv_p:
            self.status.setText("Provide only one of --input or --advance")
            return

        self.pending_tokens = list(tokens)
        try:
            self.w.close()
        except Exception:
            pass
        try:
            self.qt.QtWidgets.QApplication.quit()
        except Exception:
            pass


def run_gui() -> None:
    qt = _import_qt()
    QtWidgets = qt.QtWidgets

    app = QtWidgets.QApplication.instance()
    owns_app = app is None
    if app is None:
        app = QtWidgets.QApplication(list(sys.argv))

    win = LauncherWindow(qt)
    win.show()

    if owns_app:
        app.exec()

    tokens = getattr(win, "pending_tokens", None)
    if tokens:
        from ..app import build_arg_parser, run_from_args

        ap = build_arg_parser()
        args = ap.parse_args(list(tokens))
        try:
            setattr(args, "gui", False)
        except Exception:
            pass
        run_from_args(args, argv_tokens=list(tokens))
