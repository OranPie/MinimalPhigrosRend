#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PyQt5 app providing two tabs:
  1) PhiraInterface — Search, view, and download Phira charts
  2) PhigrosInterface — Browse community Phigros resources from GitHub and download selected difficulties/music/illustrations

Requirements (install):
    pip install PyQt5 requests

(Optional for audio preview in the Phira tab; works on most platforms that ship QtMultimedia):
    pip install PyQt5 PyQt5-Qt5 PyQt5-sip

Run:
    python pyqt_phira_phigros_interfaces.py

Notes:
- Uses GitHub API unauthenticated by default; to avoid rate limits, export GITHUB_TOKEN environment variable.
- All network calls are off the UI thread.
- Downloads show progress; you can choose destination folders.
"""
from __future__ import annotations

import os
import re
import sys
import math
import json
import time
import queue
import typing as T
import threading
from dataclasses import dataclass

import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

from PyQt5 import QtCore, QtGui, QtWidgets

from . import chart_provider_core as core

# Try to import QtMultimedia (optional)
try:
    from PyQt5 import QtMultimedia
    HAS_MULTIMEDIA = True
except Exception:
    HAS_MULTIMEDIA = False

# ----------------------------- Utilities -----------------------------

USER_AGENT = "PhiraPhigrosUI/1.0 (+https://example.local)"

def build_requests_session() -> requests.Session:
    s = requests.Session()
    retries = Retry(total=5, backoff_factor=0.4, status_forcelist=[429, 500, 502, 503, 504])
    s.mount("https://", HTTPAdapter(max_retries=retries))
    s.headers.update({"User-Agent": USER_AGENT})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        s.headers.update({"Authorization": f"Bearer {token}"})
    return s

HTTP = build_requests_session()

# ----------------------------- Data Models -----------------------------

@dataclass
class PhiraChart:
    id: int
    name: str
    level: str
    charter: str
    composer: str
    illustrator: str
    description: str
    illustration: str
    preview: str
    file: str
    created: str
    updated: str
    chartUpdated: str

    @staticmethod
    def from_json(d: dict) -> "PhiraChart":
        return PhiraChart(
            id=d.get("id"),
            name=d.get("name", ""),
            level=d.get("level", ""),
            charter=d.get("charter", ""),
            composer=d.get("composer", ""),
            illustrator=d.get("illustrator", ""),
            description=d.get("description", ""),
            illustration=d.get("illustration", ""),
            preview=d.get("preview", ""),
            file=d.get("file", ""),
            created=d.get("created", ""),
            updated=d.get("updated", ""),
            chartUpdated=d.get("chartUpdated", ""),
        )

# ----------------------------- Network Workers -----------------------------

class WorkerSignals(QtCore.QObject):
    finished = QtCore.pyqtSignal(object)
    error = QtCore.pyqtSignal(str)
    progress = QtCore.pyqtSignal(int)

class ApiWorker(QtCore.QRunnable):
    """Generic API worker that calls a function and returns its result."""
    def __init__(self, fn: T.Callable, *args, **kwargs):
        super().__init__()
        self.fn = fn
        self.args = args
        self.kwargs = kwargs
        self.signals = WorkerSignals()

    @QtCore.pyqtSlot()
    def run(self):
        try:
            res = self.fn(*self.args, **self.kwargs)
            self.signals.finished.emit(res)
        except Exception as e:
            self.signals.error.emit(str(e))

class DownloadWorker(QtCore.QRunnable):
    def __init__(self, url: str, dest_path: str):
        super().__init__()
        self.url = url
        self.dest = dest_path
        self.signals = WorkerSignals()

    @QtCore.pyqtSlot()
    def run(self):
        try:
            def _cb(pct: int) -> None:
                try:
                    self.signals.progress.emit(int(pct))
                except Exception:
                    pass

            core.download_file(url=self.url, dest_path=self.dest, progress_cb=_cb, session=core.HTTP)
            self.signals.finished.emit(self.dest)
        except Exception as e:
            self.signals.error.emit(str(e))

# ----------------------------- Phira Client -----------------------------

class PhiraClient:
    BASE = "https://phira.5wyxi.com"

    @staticmethod
    def search(pageNum=28, page=1, order="-updated", division=None,
               rating_min: float | None = None, rating_max: float | None = None,
               keyword: str | None = None) -> dict:
        params = {"pageNum": pageNum, "page": page, "order": order}
        if division:
            params["division"] = division
        if rating_min is not None and rating_max is not None:
            rating_min = max(0.0, min(1.0, float(rating_min)))
            rating_max = max(0.0, min(1.0, float(rating_max)))
            params["rating"] = f"{rating_min},{rating_max}"
        if keyword:
            params["search"] = keyword
        url = f"{PhiraClient.BASE}/chart"
        resp = HTTP.get(url, params=params, timeout=20)
        resp.raise_for_status()
        return resp.json()

    @staticmethod
    def get_chart(chart_id: int) -> PhiraChart:
        url = f"{PhiraClient.BASE}/chart/{chart_id}"
        resp = HTTP.get(url, timeout=20)
        resp.raise_for_status()
        return PhiraChart.from_json(resp.json())

# ----------------------------- Phigros GitHub Client -----------------------------

class PhigrosClient:
    OWNER = "7aGiven"
    REPO = "Phigros_Resource"
    BRANCHES = {
        "chart": "chart",
        "music": "music",
        "illustration": "illustration",
    }

    @staticmethod
    def github_api(path: str, params: dict | None = None) -> dict:
        url = f"https://api.github.com/repos/{PhigrosClient.OWNER}/{PhigrosClient.REPO}/{path}"
        r = HTTP.get(url, params=params or {}, timeout=30)
        r.raise_for_status()
        return r.json()

    @staticmethod
    def fetch_tree(branch: str) -> list[dict]:
        # Use the Git Data API to list recursively
        data = PhigrosClient.github_api(f"git/trees/{branch}", params={"recursive": 1})
        if data.get("truncated"):
            # Fallback to non-recursive, but we still return what we have
            pass
        return data.get("tree", [])

    @staticmethod
    def raw_url(branch: str, path: str) -> str:
        return f"https://raw.githubusercontent.com/{PhigrosClient.OWNER}/{PhigrosClient.REPO}/{branch}/{path}"

    SONG_RX = re.compile(r"^([^/]+)\.([^/]+)\.0/([^/]+)\.json$")

    @staticmethod
    def index_charts(tree: list[dict]) -> dict:
        """Return mapping base_key -> {song, composer, diffs: [difficulty], paths: {diff: path}}"""
        idx: dict[str, dict] = {}
        for ent in tree:
            if ent.get("type") != "blob":
                continue
            path = ent.get("path", "")
            m = PhigrosClient.SONG_RX.match(path)
            if not m:
                continue
            song, composer, diff = m.groups()
            base = f"{song}.{composer}"
            d = idx.setdefault(base, {"song": song, "composer": composer, "diffs": [], "paths": {}})
            if diff not in d["diffs"]:
                d["diffs"].append(diff)
            d["paths"][diff] = path
        # Sort diffs alphabetically with a friendly order: EZ < HD < IN < AT < EX ... fallback lexicographic
        def diff_key(x: str) -> tuple:
            order = {"EZ": 0, "HD": 1, "IN": 2, "AT": 3, "SP": 4, "EX": 5}
            return (order.get(x.upper(), 99), x.upper())
        for d in idx.values():
            d["diffs"].sort(key=diff_key)
        return idx

    @staticmethod
    def find_asset_path(tree: list[dict], base: str, allowed_exts: tuple[str, ...]) -> str | None:
        prefix = f"{base}"
        for ent in tree:
            if ent.get("type") != "blob":
                continue
            p = ent.get("path", "")
            if not p.startswith(prefix):
                continue
            ext = os.path.splitext(p)[1].lower()
            if ext in allowed_exts:  # ensure top-level under root
                return p
        return None


# Reuse the backend-neutral core session/clients to avoid duplicated logic.
# These overrides must be placed AFTER local class definitions, otherwise the
# local definitions will overwrite the aliases.
HTTP = core.HTTP
PhiraChart = core.PhiraChart
PhiraClient = core.PhiraClient
PhigrosClient = core.PhigrosClient

# ----------------------------- UI: PhiraInterface -----------------------------

class PhiraInterface(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.pool = QtCore.QThreadPool(self)
        self._build_ui()

    # UI construction
    def _build_ui(self):
        layout = QtWidgets.QVBoxLayout(self)

        # Controls row
        controls = QtWidgets.QHBoxLayout()
        self.edit_keyword = QtWidgets.QLineEdit()
        self.edit_keyword.setPlaceholderText("Search keyword…")

        self.combo_order = QtWidgets.QComboBox()
        self.combo_order.addItems(["-updated", "updated", "-rating", "rating", "name", "-name"])

        self.combo_division = QtWidgets.QComboBox()
        self.combo_division.addItems(["(all)", "plain", "troll", "visual"])  # empty means all

        self.spin_rating_min = QtWidgets.QDoubleSpinBox()
        self.spin_rating_min.setRange(0.0, 1.0)
        self.spin_rating_min.setSingleStep(0.05)
        self.spin_rating_min.setDecimals(2)
        self.spin_rating_min.setPrefix("min ")

        self.spin_rating_max = QtWidgets.QDoubleSpinBox()
        self.spin_rating_max.setRange(0.0, 1.0)
        self.spin_rating_max.setSingleStep(0.05)
        self.spin_rating_max.setDecimals(2)
        self.spin_rating_max.setValue(1.0)
        self.spin_rating_max.setPrefix("max ")

        self.spin_page_num = QtWidgets.QSpinBox()
        self.spin_page_num.setRange(4, 64)
        self.spin_page_num.setValue(28)
        self.spin_page = QtWidgets.QSpinBox()
        self.spin_page.setRange(1, 9999)
        self.btn_search = QtWidgets.QPushButton("Search")
        self.btn_prev = QtWidgets.QPushButton("◀ Prev")
        self.btn_next = QtWidgets.QPushButton("Next ▶")

        for w in [self.edit_keyword, self.combo_order, self.combo_division,
                  self.spin_rating_min, self.spin_rating_max, self.spin_page_num,
                  self.spin_page, self.btn_search, self.btn_prev, self.btn_next]:
            controls.addWidget(w)
        controls.addStretch(1)
        layout.addLayout(controls)

        # Splitter results/detail
        split = QtWidgets.QSplitter()
        split.setOrientation(QtCore.Qt.Horizontal)

        # Results list
        self.list_results = QtWidgets.QListWidget()
        self.list_results.setIconSize(QtCore.QSize(96, 96))
        self.list_results.itemSelectionChanged.connect(self._on_result_selected)

        # Detail panel
        self.detail = QtWidgets.QWidget()
        dlay = QtWidgets.QVBoxLayout(self.detail)
        self.lbl_title = QtWidgets.QLabel("<b>Select a chart</b>")
        self.lbl_meta = QtWidgets.QLabel("")
        self.lbl_meta.setWordWrap(True)
        self.lbl_cover = QtWidgets.QLabel()
        self.lbl_cover.setAlignment(QtCore.Qt.AlignCenter)
        self.lbl_cover.setMinimumHeight(200)
        self.lbl_cover.setStyleSheet("background:#222;color:#ccc;")

        self.text_desc = QtWidgets.QTextEdit()
        self.text_desc.setReadOnly(True)

        self.btn_open = QtWidgets.QPushButton("Open Chart Web Page")
        self.btn_download = QtWidgets.QPushButton("Download ZIP…")
        self.progress = QtWidgets.QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.hide()

        d_buttons = QtWidgets.QHBoxLayout()
        d_buttons.addWidget(self.btn_open)
        d_buttons.addWidget(self.btn_download)
        d_buttons.addStretch(1)

        dlay.addWidget(self.lbl_title)
        dlay.addWidget(self.lbl_meta)
        dlay.addWidget(self.lbl_cover)
        if HAS_MULTIMEDIA:
            # basic audio preview controls
            audio_row = QtWidgets.QHBoxLayout()
            self.btn_play = QtWidgets.QPushButton("▶ Preview")
            self.btn_stop = QtWidgets.QPushButton("■ Stop")
            self.btn_stop.setEnabled(False)
            audio_row.addWidget(self.btn_play)
            audio_row.addWidget(self.btn_stop)
            audio_row.addStretch(1)
            dlay.addLayout(audio_row)
        dlay.addWidget(self.text_desc, 1)
        dlay.addLayout(d_buttons)
        dlay.addWidget(self.progress)

        split.addWidget(self.list_results)
        split.addWidget(self.detail)
        split.setStretchFactor(0, 3)
        split.setStretchFactor(1, 5)
        layout.addWidget(split, 1)

        # Signals
        self.btn_search.clicked.connect(self._do_search)
        self.btn_prev.clicked.connect(self._prev_page)
        self.btn_next.clicked.connect(self._next_page)
        self.btn_open.clicked.connect(self._open_chart_page)
        self.btn_download.clicked.connect(self._download_zip)

        if HAS_MULTIMEDIA:
            self.player = QtMultimedia.QMediaPlayer()
            self.audio_output = None
            try:
                # Some PyQt5 builds require explicit audio output setup
                self.audio_output = QtMultimedia.QAudioOutput()
                self.player.setAudioOutput(self.audio_output)  # type: ignore[attr-defined]
            except Exception:
                pass
            self.btn_play.clicked.connect(self._play_preview)
            self.btn_stop.clicked.connect(self._stop_preview)

        # Local state
        self.current_results: list[PhiraChart] = []
        self.current_count = 0

    # Helpers
    def _params(self):
        division = self.combo_division.currentText()
        if division == "(all)":
            division = None
        return dict(
            pageNum=self.spin_page_num.value(),
            page=self.spin_page.value(),
            order=self.combo_order.currentText(),
            division=division,
            rating_min=self.spin_rating_min.value(),
            rating_max=self.spin_rating_max.value(),
            keyword=self.edit_keyword.text().strip() or None,
        )

    def _do_search(self):
        worker = ApiWorker(PhiraClient.search, **self._params())
        worker.signals.finished.connect(self._on_search_result)
        worker.signals.error.connect(self._on_error)
        self.pool.start(worker)

    def _prev_page(self):
        p = self.spin_page.value()
        if p > 1:
            self.spin_page.setValue(p - 1)
            self._do_search()

    def _next_page(self):
        self.spin_page.setValue(self.spin_page.value() + 1)
        self._do_search()

    def _on_search_result(self, data: dict):
        self.list_results.clear()
        res = data.get("results", [])
        self.current_results = [PhiraChart.from_json(x) for x in res]
        self.current_count = int(data.get("count", 0))
        # Update window title / pager hint
        page = self.spin_page.value()
        per = self.spin_page_num.value()
        total_pages = math.ceil(self.current_count / per) if per else 1
        hint = f"Page {page}/{max(1, total_pages)} — {self.current_count} results"
        self.lbl_title.setText(hint)

        for item in self.current_results:
            lw = QtWidgets.QListWidgetItem()
            lw.setText(f"{item.name}  |  {item.level}\n{item.charter} · {item.composer}")
            # Try to fetch illustration quickly (non-blocking via QPixmap.loadFromData in a thread?)
            icon = QtGui.QIcon()
            if item.illustration:
                try:
                    img = HTTP.get(item.illustration, timeout=10)
                    img.raise_for_status()
                    pm = QtGui.QPixmap()
                    pm.loadFromData(img.content)
                    icon = QtGui.QIcon(pm)
                except Exception:
                    pass
            lw.setIcon(icon)
            self.list_results.addItem(lw)

        if not self.current_results:
            self.lbl_meta.setText("No results on this page.")
        else:
            self.lbl_meta.setText("Select a chart to see details.")

    def _on_error(self, msg: str):
        QtWidgets.QMessageBox.critical(self, "Error", msg)

    # Selection
    def _on_result_selected(self):
        rows = self.list_results.selectedIndexes()
        if not rows:
            return
        row = rows[0].row()
        if not (0 <= row < len(self.current_results)):
            return
        c = self.current_results[row]
        self._show_chart(c)

    def _show_chart(self, c: PhiraChart):
        self.lbl_title.setText(f"<b>{QtWidgets.QApplication.translate('', c.name)}</b>")
        meta = (
            f"ID: {c.id}  |  Level: {c.level}<br>"
            f"Charter: {c.charter}  |  Composer: {c.composer}<br>"
            f"Updated: {c.updated}  |  ChartUpdated: {c.chartUpdated}"
        )
        self.lbl_meta.setText(meta)
        # Cover
        if c.illustration:
            try:
                img = HTTP.get(c.illustration, timeout=10)
                img.raise_for_status()
                pm = QtGui.QPixmap()
                pm.loadFromData(img.content)
                self.lbl_cover.setPixmap(pm.scaledToHeight(280, QtCore.Qt.SmoothTransformation))
            except Exception:
                self.lbl_cover.setText("(cover unavailable)")
        else:
            self.lbl_cover.setText("(no cover)")
        # Description
        self.text_desc.setPlainText(c.description or "(no description)")
        # Configure preview
        if HAS_MULTIMEDIA and c.preview:
            try:
                url = QtCore.QUrl(c.preview)
                if hasattr(self.player, "setSource"):
                    self.player.setSource(url)  # Qt6 style (some PyQt5 backports)
                else:
                    self.player.setMedia(QtMultimedia.QMediaContent(url))  # Qt5
                self.btn_play.setEnabled(True)
                self.btn_stop.setEnabled(True)
            except Exception:
                self.btn_play.setEnabled(False)
                self.btn_stop.setEnabled(False)
        self._selected_chart = c

    def _open_chart_page(self):
        c = getattr(self, "_selected_chart", None)
        if not c:
            return
        url = QtCore.QUrl(f"https://phira.5wyxi.com/chart/{c.id}")
        QtGui.QDesktopServices.openUrl(url)

    def _download_zip(self):
        c = getattr(self, "_selected_chart", None)
        if not c:
            return
        dest_dir = QtWidgets.QFileDialog.getExistingDirectory(self, "Choose download folder")
        if not dest_dir:
            return
        safe_name = re.sub(r"[^\w\-\.]+", "_", f"{c.name}.{c.charter}.{c.id}")
        dest = os.path.join(dest_dir, f"{safe_name}.zip")
        worker = DownloadWorker(c.file, dest)
        self.progress.show()
        self.progress.setValue(0)
        worker.signals.progress.connect(self.progress.setValue)
        def done(path):
            self.progress.hide()
            QtWidgets.QMessageBox.information(self, "Download complete", f"Saved to:\n{path}")
        worker.signals.finished.connect(done)
        worker.signals.error.connect(self._on_error)
        self.pool.start(worker)

    # Audio controls
    def _play_preview(self):
        if not HAS_MULTIMEDIA:
            return
        try:
            self.player.play()
        except Exception:
            pass

    def _stop_preview(self):
        if not HAS_MULTIMEDIA:
            return
        try:
            self.player.stop()
        except Exception:
            pass

# ----------------------------- UI: PhigrosInterface -----------------------------

class PhigrosInterface(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.pool = QtCore.QThreadPool(self)
        self._chart_index: dict[str, dict] = {}
        self._music_tree: list[dict] = []
        self._illustration_tree: list[dict] = []
        self._build_ui()
        self._load_index()

    def _build_ui(self):
        layout = QtWidgets.QVBoxLayout(self)

        # Toolbar
        tb = QtWidgets.QHBoxLayout()
        self.edit_filter = QtWidgets.QLineEdit()
        self.edit_filter.setPlaceholderText("Filter by song/composer/difficulty…")
        self.btn_refresh = QtWidgets.QPushButton("Refresh Index")
        self.chk_assets = QtWidgets.QCheckBox("Also download music & illustration")
        self.chk_assets.setChecked(True)
        tb.addWidget(self.edit_filter, 3)
        tb.addWidget(self.chk_assets)
        tb.addWidget(self.btn_refresh)
        layout.addLayout(tb)

        # Results table
        self.table = QtWidgets.QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["Song", "Composer", "Difficulties", "Base Key"])
        self.table.horizontalHeader().setSectionResizeMode(0, QtWidgets.QHeaderView.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(1, QtWidgets.QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QtWidgets.QHeaderView.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(3, QtWidgets.QHeaderView.ResizeToContents)
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        layout.addWidget(self.table, 1)

        # Bottom bar
        bb = QtWidgets.QHBoxLayout()
        self.combo_diff = QtWidgets.QComboBox()
        self.combo_diff.setMinimumWidth(160)
        self.btn_download = QtWidgets.QPushButton("Download Selected…")
        bb.addWidget(QtWidgets.QLabel("Difficulty:"))
        bb.addWidget(self.combo_diff)
        bb.addStretch(1)
        bb.addWidget(self.btn_download)
        layout.addLayout(bb)

        # Signals
        self.btn_refresh.clicked.connect(self._load_index)
        self.edit_filter.textChanged.connect(self._refilter)
        self.table.itemSelectionChanged.connect(self._on_row_selected)
        self.btn_download.clicked.connect(self._download_selected)

    # Loading index from GitHub
    def _load_index(self):
        self.table.setRowCount(0)
        self.combo_diff.clear()

        def task():
            chart_tree = PhigrosClient.fetch_tree(PhigrosClient.BRANCHES["chart"])
            music_tree = PhigrosClient.fetch_tree(PhigrosClient.BRANCHES["music"])
            illu_tree  = PhigrosClient.fetch_tree(PhigrosClient.BRANCHES["illustration"])
            idx = PhigrosClient.index_charts(chart_tree)
            return idx, music_tree, illu_tree

        worker = ApiWorker(task)
        def done(result):
            idx, self._music_tree, self._illustration_tree = result
            self._chart_index = idx
            self._populate_table()
        worker.signals.finished.connect(done)
        worker.signals.error.connect(self._on_error)
        self.pool.start(worker)

    def _populate_table(self):
        filt = self.edit_filter.text().strip().lower()
        rows = []
        for base, d in self._chart_index.items():
            song = d["song"]
            composer = d["composer"]
            diffs = d["diffs"]
            disp = ", ".join(diffs)
            hay = f"{song} {composer} {disp} {base}".lower()
            if filt and filt not in hay:
                continue
            rows.append((song, composer, disp, base))
        rows.sort(key=lambda r: (r[0].lower(), r[1].lower()))
        self.table.setRowCount(len(rows))
        for i, (song, composer, disp, base) in enumerate(rows):
            self.table.setItem(i, 0, QtWidgets.QTableWidgetItem(song))
            self.table.setItem(i, 1, QtWidgets.QTableWidgetItem(composer))
            self.table.setItem(i, 2, QtWidgets.QTableWidgetItem(disp))
            self.table.setItem(i, 3, QtWidgets.QTableWidgetItem(base))
        if rows:
            self.table.selectRow(0)

    def _refilter(self):
        self._populate_table()

    def _on_row_selected(self):
        rows = self.table.selectionModel().selectedRows()
        self.combo_diff.clear()
        if not rows:
            return
        base = self.table.item(rows[0].row(), 3).text()
        diffs = self._chart_index.get(base, {}).get("diffs", [])
        self.combo_diff.addItems(diffs)

    def _pick_dest_dir(self) -> str:
        return QtWidgets.QFileDialog.getExistingDirectory(self, "Choose download folder")

    def _download_selected(self):
        rows = self.table.selectionModel().selectedRows()
        if not rows:
            return
        base = self.table.item(rows[0].row(), 3).text()
        diff = self.combo_diff.currentText()
        if not diff:
            QtWidgets.QMessageBox.warning(self, "No difficulty", "Please pick a difficulty.")
            return
        dest_dir = self._pick_dest_dir()
        if not dest_dir:
            return
        # Compose downloads
        chart_path = self._chart_index[base]["paths"].get(diff)
        if not chart_path:
            QtWidgets.QMessageBox.critical(self, "Missing chart", "Chart path not found.")
            return
        chart_url = PhigrosClient.raw_url(PhigrosClient.BRANCHES["chart"], chart_path)
        # Assets (optional)
        music_path = None
        illu_path = None
        if self.chk_assets.isChecked():
            music_path = PhigrosClient.find_asset_path(self._music_tree, base, (".ogg", ".mp3", ".wav"))
            illu_path = PhigrosClient.find_asset_path(self._illustration_tree, base, (".png", ".jpg", ".jpeg", ".webp"))
        jobs: list[tuple[str, str]] = []  # (url, dest)
        safe_base = re.sub(r"[^\w\-\.]+", "_", base)
        # Ensure subdir per song
        out_dir = os.path.join(dest_dir, safe_base)
        os.makedirs(out_dir, exist_ok=True)
        jobs.append((chart_url, os.path.join(out_dir, f"{diff}.json")))
        if music_path:
            jobs.append((PhigrosClient.raw_url(PhigrosClient.BRANCHES["music"], music_path),
                         os.path.join(out_dir, os.path.basename(music_path))))
        if illu_path:
            jobs.append((PhigrosClient.raw_url(PhigrosClient.BRANCHES["illustration"], illu_path),
                         os.path.join(out_dir, os.path.basename(illu_path))))

        self._run_batch_download(jobs)

    def _run_batch_download(self, jobs: list[tuple[str, str]]):
        if not jobs:
            QtWidgets.QMessageBox.information(self, "Nothing to download", "No files found for this selection.")
            return
        dlg = BatchDownloadDialog(jobs, self)
        dlg.exec_()

    def _on_error(self, msg: str):
        QtWidgets.QMessageBox.critical(self, "Error", msg)

# ----------------------------- Batch Download Dialog -----------------------------

class BatchDownloadDialog(QtWidgets.QDialog):
    def __init__(self, jobs: list[tuple[str, str]], parent=None):
        super().__init__(parent)
        self.setWindowTitle("Downloading…")
        self.jobs = jobs
        self.pool = QtCore.QThreadPool(self)
        self._build_ui()
        self._start()

    def _build_ui(self):
        lay = QtWidgets.QVBoxLayout(self)
        self.list = QtWidgets.QListWidget()
        for url, dest in self.jobs:
            it = QtWidgets.QListWidgetItem(f"{os.path.basename(dest)}\n→ {url}")
            it.setData(QtCore.Qt.UserRole, (url, dest))
            self.list.addItem(it)
        self.progress = QtWidgets.QProgressBar()
        self.progress.setRange(0, len(self.jobs))
        self.lbl = QtWidgets.QLabel("Starting…")
        btns = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Close)
        btns.rejected.connect(self.reject)
        lay.addWidget(self.list)
        lay.addWidget(self.progress)
        lay.addWidget(self.lbl)
        lay.addWidget(btns)
        self.resize(720, 420)

    def _start(self):
        self.completed = 0
        for i in range(self.list.count()):
            url, dest = self.list.item(i).data(QtCore.Qt.UserRole)
            worker = DownloadWorker(url, dest)
            worker.signals.finished.connect(self._one_done)
            worker.signals.error.connect(self._one_err)
            self.parent().pool.start(worker)  # use parent's pool to limit concurrency

    def _one_done(self, path: str):
        self.completed += 1
        self.progress.setValue(self.completed)
        self.lbl.setText(f"Completed {self.completed}/{self.progress.maximum()}…")
        if self.completed >= self.progress.maximum():
            self.lbl.setText("All done.")

    def _one_err(self, msg: str):
        QtWidgets.QMessageBox.critical(self, "Download error", msg)

# ----------------------------- Main Window -----------------------------

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Phira & Phigros Interfaces")
        self.resize(1200, 800)
        tabs = QtWidgets.QTabWidget()
        tabs.addTab(PhiraInterface(), "Phira")
        tabs.addTab(PhigrosInterface(), "Phigros")
        self.setCentralWidget(tabs)

        # Light styling
        self.setStyleSheet("""
        QTabWidget::pane { border: 0; }
        QListWidget::item { padding: 8px; }
        QPushButton { padding: 6px 10px; }
        """)

# ----------------------------- Entrypoint -----------------------------

def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()
