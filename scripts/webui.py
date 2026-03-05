#!/usr/bin/env python3
"""
Phigros Render — Enhanced Web UI
Two render modes:
  1. Preview   — headless + --screenshot-fps, streams frames live to browser
  2. Export    — headless + --record, produces downloadable MP4

Usage:
    python3 scripts/webui.py [--port 8080] [--binary cpp/build/phigros_render]
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import shutil
import subprocess
import threading
import time
import uuid
import zipfile
from pathlib import Path

try:
    from flask import (Flask, Response, jsonify, request,
                       send_file, stream_with_context)
except ImportError:
    raise SystemExit("Flask not found — run: pip install flask")

# ── Paths ─────────────────────────────────────────────────────────────────────
_HERE   = Path(__file__).resolve().parent
_ROOT   = _HERE.parent
_CHARTS = _ROOT / "charts"
_BIN    = _ROOT / "cpp" / "build" / "phigros_render"
_RESPACK = _ROOT / "respack.zip"
_TMP    = Path("/tmp/phigros_webui")
_TMP.mkdir(parents=True, exist_ok=True)
_WASM_DIR  = _ROOT / "cpp" / "build_wasm"
_SHELL_HTML = _ROOT / "cpp" / "web" / "shell.html"

DIFF_ORDER = {"EZ": 0, "HD": 1, "IN": 2, "AT": 3}

# ── Chart discovery ────────────────────────────────────────────────────────────
def _parse_diff(stem: str) -> str:
    return {"EZ": "EZ", "HD": "HD", "IN": "IN", "AT": "AT",
            "ez": "EZ", "hd": "HD", "in": "IN", "at": "AT"}.get(stem, stem.upper()[:2])

def discover_charts(charts_dir: Path = _CHARTS) -> list[dict]:
    results = []
    if not charts_dir.is_dir():
        return results
    for folder in sorted(charts_dir.iterdir()):
        if not folder.is_dir():
            continue
        jsons = sorted(folder.glob("*.json"))
        if not jsons:
            continue
        audio = (next(iter(folder.glob("*.ogg")), None) or
                 next(iter(folder.glob("*.mp3")), None))
        bg    = (next(iter(folder.glob("*.png")), None) or
                 next(iter(folder.glob("*.jpg")), None))
        for jf in jsons:
            diff = _parse_diff(jf.stem)
            results.append({
                "id":        f"{folder.name}/{jf.name}",
                "name":      folder.name,
                "diff":      diff,
                "sort":      DIFF_ORDER.get(diff, 99),
                "path":      str(jf),
                "rel":       f"charts/{folder.name}/{jf.name}",
                "audio":     str(audio) if audio else "",
                "bg":        str(bg) if bg else "",
                "bg_url":    f"/asset/{folder.name}/{bg.name}" if bg else "",
                "audio_url": "",
            })
    for zf_path in sorted(charts_dir.glob("*.zip")):
        try:
            with zipfile.ZipFile(zf_path) as zf:
                names = zf.namelist()
        except Exception:
            continue
        jsons      = sorted(n for n in names if n.endswith(".json") and "/" not in n)
        audio_name = next((n for n in names if n.lower().endswith((".ogg", ".mp3", ".wav", ".flac"))), None)
        bg_name    = next((n for n in names if n.lower().endswith((".png", ".jpg", ".jpeg", ".webp"))), None)
        if not jsons:
            continue
        for jname in jsons:
            diff = _parse_diff(Path(jname).stem)
            results.append({
                "id":        f"{zf_path.name}/{jname}",
                "name":      zf_path.stem,
                "diff":      diff,
                "sort":      DIFF_ORDER.get(diff, 99),
                "path":      str(zf_path),
                "rel":       f"charts/{zf_path.name}/{jname}",
                "audio":     "",
                "bg":        "",
                "bg_url":    f"/zip-asset/{zf_path.name}/{bg_name}" if bg_name else "",
                "audio_url": f"/zip-asset/{zf_path.name}/{audio_name}" if audio_name else "",
                "zip_chart": jname,
                "zip_audio": audio_name or "",
                "zip_bg":    bg_name or "",
            })
    return results

# ── Job management ─────────────────────────────────────────────────────────────
_jobs: dict[str, "Job"] = {}
_jobs_lock = threading.Lock()

class Job:
    def __init__(self, job_id: str, mode: str, chart: dict, cfg: dict):
        self.id       = job_id
        self.mode     = mode        # "preview" | "export"
        self.chart    = chart
        self.cfg      = cfg
        self.status   = "pending"   # pending|running|done|error|cancelled
        self.progress = 0.0
        self.frames: list[str] = [] # PNG paths (preview mode)
        self.output: str = ""       # MP4 path (export mode)
        self.logs: list[dict] = []
        self._lock    = threading.Lock()
        self._proc: subprocess.Popen | None = None
        self._workdir = str(_TMP / job_id)
        os.makedirs(self._workdir, exist_ok=True)

    def _add_log(self, msg: str, level: str = "info"):
        with self._lock:
            self.logs.append({"t": time.time(), "msg": msg, "level": level})

    def _build_cmd(self) -> list[str]:
        cfg   = self.cfg
        chart = self.chart

        # Resolve chart, audio, bg paths (extract from zip if needed)
        chart_path = chart["path"]
        audio_path = chart.get("audio", "")
        bg_path    = chart.get("bg", "")

        if chart.get("zip_chart"):
            with zipfile.ZipFile(chart_path) as zf:
                zf.extract(chart["zip_chart"], self._workdir)
                chart_path = str(Path(self._workdir) / chart["zip_chart"])
                if chart.get("zip_audio"):
                    zf.extract(chart["zip_audio"], self._workdir)
                    audio_path = str(Path(self._workdir) / chart["zip_audio"])
                if chart.get("zip_bg"):
                    zf.extract(chart["zip_bg"], self._workdir)
                    bg_path = str(Path(self._workdir) / chart["zip_bg"])

        cmd = [str(_BIN), chart_path, "--headless"]

        if self.mode == "preview":
            cmd += ["--screenshot-dir", self._workdir,
                    "--screenshot-fps", str(cfg.get("preview_fps", 1))]
        else:  # export
            self.output = str(Path(self._workdir) / "output.mp4")
            cmd += ["--record", self.output,
                    "--record-preset", cfg.get("record_preset", "balanced")]
            if cfg.get("record_fps"):
                cmd += ["--record-fps", str(cfg["record_fps"])]
            if cfg.get("record_codec"):
                cmd += ["--record-codec", cfg["record_codec"]]
            if cfg.get("record_resolution"):
                cmd += ["--record-resolution", cfg["record_resolution"]]

        if _RESPACK.exists():
            cmd += ["--respack", str(_RESPACK)]
        if audio_path:
            cmd += ["--audio", audio_path]
        if bg_path:
            cmd += ["--bg", bg_path]
        if cfg.get("width") and cfg.get("height"):
            cmd += ["--width", str(cfg["width"]), "--height", str(cfg["height"])]
        if cfg.get("expand", 1.0) != 1.0:
            cmd += ["--expand", str(cfg["expand"])]
        if cfg.get("duration"):
            cmd += ["--duration", str(cfg["duration"])]
        if cfg.get("no_cull"):
            cmd += ["--no-cull"]
        if cfg.get("no_cull_screen"):
            cmd += ["--no-cull-screen"]
        if cfg.get("no_cull_enter_time"):
            cmd += ["--no-cull-enter-time"]
        if cfg.get("trail"):
            cmd += ["--trail"]
        if cfg.get("motion_blur"):
            cmd += ["--motion-blur"]
        return cmd

    def _watch_frames(self):
        """Background thread: watch workdir for new PNGs (preview mode)."""
        seen: set[str] = set()
        while self.status == "running":
            try:
                for f in sorted(Path(self._workdir).glob("*.png")):
                    if str(f) not in seen:
                        seen.add(str(f))
                        with self._lock:
                            self.frames.append(str(f))
            except Exception:
                pass
            time.sleep(0.25)
        # final scan
        try:
            for f in sorted(Path(self._workdir).glob("*.png")):
                if str(f) not in seen:
                    seen.add(str(f))
                    with self._lock:
                        self.frames.append(str(f))
        except Exception:
            pass

    _NOISE = ("ALSA", "MESA", "jack", "pci", "kms", "Shm", "bochs",
              "Cannot connect", "failed to load driver", "Failed to load BGM")

    def run(self):
        self.status = "running"
        cmd = self._build_cmd()
        self._add_log("$ " + " ".join(cmd), "debug")

        if self.mode == "preview":
            watcher = threading.Thread(target=self._watch_frames, daemon=True)
            watcher.start()

        try:
            self._proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=str(_ROOT),
            )
            for raw in self._proc.stdout:          # type: ignore[union-attr]
                line = raw.rstrip()
                if not line or any(n in line for n in self._NOISE):
                    continue
                lvl = "error" if re.search(r"\b(error|fail|failed)\b", line, re.I) else "info"
                if re.search(r"\[Record\]|\[Chart\]|Render Complete|Score:|Accuracy:|MaxCombo:", line):
                    lvl = "success"
                # parse Record progress
                m = re.search(r"\[Record\]\s+([\d.]+)%", line)
                if m:
                    self.progress = float(m.group(1))
                self._add_log(line, lvl)
            self._proc.wait()
            rc = self._proc.returncode
        except Exception as exc:
            self._add_log(f"[Error] {exc}", "error")
            self.status = "error"
            return

        # rc 0 = success (perfect score); rc 1 = success (non-perfect score, normal in headless mode)
        # rc 2+ = actual error
        if rc in (0, 1):
            self.status = "done"
            self.progress = 100.0
            self._add_log("✓ Render complete", "success")
            if self.mode == "export" and Path(self.output).exists():
                sz = Path(self.output).stat().st_size
                self._add_log(f"  Output: {self.output} ({sz//1024} KB)", "success")
        elif self.status != "cancelled":
            self.status = "error"
            self._add_log(f"[Error] Process exited with code {rc}", "error")

    def cancel(self):
        self.status = "cancelled"
        if self._proc:
            try:
                self._proc.terminate()
            except Exception:
                pass

    def cleanup(self):
        try:
            shutil.rmtree(self._workdir, ignore_errors=True)
        except Exception:
            pass

    def to_dict(self) -> dict:
        with self._lock:
            return {
                "id":       self.id,
                "mode":     self.mode,
                "status":   self.status,
                "progress": self.progress,
                "n_frames": len(self.frames),
                "has_output": bool(self.output and Path(self.output).exists()),
            }

# ── Flask app ──────────────────────────────────────────────────────────────────
app = Flask(__name__)

@app.route("/")
def index():
    return HTML

@app.route("/charts")
def list_charts_route():
    return jsonify(discover_charts())

@app.route("/asset/<path:rel>")
def asset(rel: str):
    p = _CHARTS / rel
    if p.exists() and p.is_file():
        return send_file(str(p))
    return "not found", 404

@app.route("/zip-asset/<zipname>/<path:filename>")
def zip_asset(zipname: str, filename: str):
    """Serve a file from inside a zip chart package."""
    zf_path = _CHARTS / zipname
    if not zf_path.is_file():
        return "not found", 404
    try:
        with zipfile.ZipFile(zf_path) as zf:
            data = zf.read(filename)
        ext  = Path(filename).suffix.lower()
        mime = {".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png",
                ".mp3": "audio/mpeg", ".ogg": "audio/ogg", ".wav": "audio/wav",
                ".flac": "audio/flac", ".json": "application/json"
               }.get(ext, "application/octet-stream")
        return Response(data, mimetype=mime)
    except Exception:
        return "not found", 404

# ── WASM browser rendering ─────────────────────────────────────────────────────
@app.route("/wasm")
def wasm_player():
    """Serve the WASM shell.html with {{{ SCRIPT }}} replaced by the JS loader."""
    if not _SHELL_HTML.exists():
        return "shell.html not found — build WASM first", 404
    html = _SHELL_HTML.read_text()
    html = html.replace("{{{ SCRIPT }}}", '<script src="/wasm/phigros_render.js"></script>')
    return Response(html, mimetype="text/html")

@app.route("/wasm/<path:filename>")
def wasm_file(filename: str):
    """Serve WASM build artifacts (.js, .wasm, .data)."""
    p = _WASM_DIR / filename
    if not p.exists() or not p.is_file():
        return "not found", 404
    mime = {".js": "application/javascript", ".wasm": "application/wasm",
            ".data": "application/octet-stream"}.get(p.suffix, "application/octet-stream")
    return send_file(str(p), mimetype=mime)

@app.route("/chart-data/<path:rel>")
def chart_data(rel: str):
    """Serve raw chart JSON for WASM player to fetch (also handles zip-internal files)."""
    p = _CHARTS / rel
    if p.exists() and p.is_file():
        return send_file(str(p), mimetype="application/json")
    # Handle zip-internal chart: rel = "Name.zip/inner.json"
    parts = Path(rel).parts
    for i in range(len(parts)):
        candidate = _CHARTS / Path(*parts[:i+1])
        if candidate.suffix == ".zip" and candidate.is_file():
            inner = str(Path(*parts[i+1:])) if i + 1 < len(parts) else ""
            if inner:
                try:
                    with zipfile.ZipFile(candidate) as zf:
                        data = zf.read(inner)
                    return Response(data, mimetype="application/json")
                except Exception:
                    pass
    return "not found", 404

@app.route("/respack-data")
def respack_data():
    """Serve respack.zip for WASM player."""
    if _RESPACK.exists():
        return send_file(str(_RESPACK), mimetype="application/zip")
    return "no respack", 404

# ── Jobs ───────────────────────────────────────────────────────────────────────
@app.route("/jobs", methods=["POST"])
def create_job():
    data  = request.get_json(force=True)
    jid   = uuid.uuid4().hex[:8]
    job   = Job(jid, data["mode"], data["chart"], data.get("config", {}))
    with _jobs_lock:
        _jobs[jid] = job
    threading.Thread(target=job.run, daemon=True).start()
    return jsonify({"id": jid}), 201

@app.route("/jobs/<jid>")
def job_status(jid: str):
    job = _jobs.get(jid)
    if not job:
        return jsonify({"error": "not found"}), 404
    return jsonify(job.to_dict())

@app.route("/jobs/<jid>", methods=["DELETE"])
def cancel_job(jid: str):
    job = _jobs.get(jid)
    if job:
        job.cancel()
    return jsonify({"cancelled": True})

@app.route("/jobs/<jid>/log")
def job_log_sse(jid: str):
    def generate():
        sent = 0
        while True:
            job = _jobs.get(jid)
            if not job:
                yield "data: " + json.dumps({"done": True}) + "\n\n"
                return
            with job._lock:
                new_logs = job.logs[sent:]
                sent     = len(job.logs)
                done     = job.status in ("done", "error", "cancelled")
            for entry in new_logs:
                yield "data: " + json.dumps(entry) + "\n\n"
            if done:
                yield "data: " + json.dumps({"done": True, "status": job.status}) + "\n\n"
                return
            time.sleep(0.15)
    return Response(
        stream_with_context(generate()),
        mimetype="text/event-stream",
        headers={"X-Accel-Buffering": "no", "Cache-Control": "no-cache"},
    )

@app.route("/jobs/<jid>/frame/<int:idx>")
def job_frame(jid: str, idx: int):
    job = _jobs.get(jid)
    if not job:
        return "not found", 404
    with job._lock:
        if 0 <= idx < len(job.frames):
            path = job.frames[idx]
    if path and Path(path).exists():
        return send_file(path, mimetype="image/png")
    return "frame not ready", 404

@app.route("/jobs/<jid>/download")
def job_download(jid: str):
    job = _jobs.get(jid)
    if not job or job.status != "done":
        return "not ready", 404
    if not job.output or not Path(job.output).exists():
        return "no output", 404
    chart_name = job.chart.get("name", "render")
    filename   = f"{chart_name}_{job.chart.get('diff','')}.mp4".replace(" ", "_")
    return send_file(job.output, as_attachment=True, download_name=filename)

# ── Embedded HTML ──────────────────────────────────────────────────────────────
HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Phigros Render</title>
<style>
:root{--bg:#0d1117;--s1:#161b22;--s2:#21262d;--border:#30363d;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff;--ok:#3fb950;--warn:#e3b341;--err:#f85149;--r:8px}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font:13px/1.55 'Segoe UI',system-ui,sans-serif;display:flex;flex-direction:column;height:100vh;overflow:hidden}
header{display:flex;align-items:center;gap:12px;padding:10px 18px;background:var(--s1);border-bottom:1px solid var(--border);flex-shrink:0}
header h1{font-size:15px;font-weight:700;letter-spacing:.3px}
header span{color:var(--muted);font-size:12px}
.main{display:flex;flex:1;overflow:hidden;gap:0}
/* ── Sidebar ── */
.sidebar{width:270px;min-width:200px;display:flex;flex-direction:column;background:var(--s1);border-right:1px solid var(--border);flex-shrink:0}
.sidebar input{margin:10px;padding:7px 10px;background:var(--s2);border:1px solid var(--border);border-radius:var(--r);color:var(--text);font-size:12px;outline:none}
.sidebar input:focus{border-color:var(--accent)}
.chart-list{flex:1;overflow-y:auto;padding:0 6px 6px}
.chart-item{display:flex;align-items:center;gap:10px;padding:8px 10px;border-radius:var(--r);cursor:pointer;transition:background .15s;border:1px solid transparent;margin-bottom:3px}
.chart-item:hover{background:var(--s2)}
.chart-item.active{background:rgba(88,166,255,.12);border-color:rgba(88,166,255,.3)}
.thumb{width:42px;height:28px;border-radius:4px;object-fit:cover;background:var(--s2);flex-shrink:0}
.chart-info{min-width:0}
.chart-name{font-size:12px;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.diff-badge{font-size:10px;padding:1px 5px;border-radius:3px;font-weight:600;display:inline-block;margin-top:2px}
.EZ{background:#1a4a1a;color:#3fb950}.HD{background:#1a3a5a;color:#58a6ff}
.IN{background:#4a1a4a;color:#d2a8ff}.AT{background:#4a1a1a;color:#f85149}
/* ── Center panel ── */
.center{flex:1;display:flex;flex-direction:column;min-width:0;overflow:hidden}
.center-top{padding:14px 16px;background:var(--s1);border-bottom:1px solid var(--border);flex-shrink:0}
.selected-chart{display:flex;align-items:center;gap:12px;margin-bottom:12px}
.selected-thumb{width:80px;height:50px;border-radius:6px;object-fit:cover;background:var(--s2)}
.selected-title{font-size:16px;font-weight:600}
.selected-sub{color:var(--muted);font-size:12px;margin-top:2px}
/* tabs */
.tabs{display:flex;gap:2px;margin-bottom:12px}
.tab{padding:5px 12px;border-radius:var(--r);font-size:12px;cursor:pointer;border:none;background:var(--s2);color:var(--muted)}
.tab.active{background:var(--accent);color:#fff}
.tab-panel{display:none}.tab-panel.active{display:block}
/* config grid */
.cfg-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px 14px}
.cfg-group{grid-column:1/-1;font-size:11px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-top:4px}
label{display:flex;flex-direction:column;gap:3px;font-size:12px;color:var(--muted)}
label span{color:var(--text)}
input[type=number],input[type=text],select{padding:5px 8px;background:var(--s2);border:1px solid var(--border);border-radius:5px;color:var(--text);font-size:12px;width:100%;outline:none}
input[type=number]:focus,input[type=text]:focus,select:focus{border-color:var(--accent)}
.toggle-row{display:flex;align-items:center;gap:8px;padding:6px 0;font-size:12px}
input[type=checkbox]{accent-color:var(--accent);width:14px;height:14px}
/* render buttons */
.render-btns{display:flex;gap:8px;margin-top:12px}
.btn{padding:7px 16px;border-radius:var(--r);font-size:13px;font-weight:600;cursor:pointer;border:none;transition:opacity .15s}
.btn:disabled{opacity:.4;cursor:not-allowed}
.btn-preview{background:var(--accent);color:#fff}
.btn-export{background:#3fb950;color:#fff}
.btn-stop{background:var(--err);color:#fff}
.btn:hover:not(:disabled){opacity:.85}
/* ── Right output panel ── */
.output{width:460px;min-width:300px;display:flex;flex-direction:column;border-left:1px solid var(--border);background:var(--s1);flex-shrink:0}
.output-header{padding:10px 14px;font-size:12px;font-weight:600;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:8px;flex-shrink:0}
.status-dot{width:8px;height:8px;border-radius:50%;background:var(--muted)}
.status-dot.running{background:var(--warn);animation:pulse 1s infinite}
.status-dot.done{background:var(--ok)}
.status-dot.error{background:var(--err)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.progress-bar-wrap{padding:8px 14px;flex-shrink:0}
.progress-bar{height:4px;background:var(--s2);border-radius:2px;overflow:hidden}
.progress-fill{height:100%;background:var(--accent);border-radius:2px;transition:width .3s}
/* log panel */
.log-panel{flex:1;overflow-y:auto;padding:8px 12px;font:12px/1.7 'Consolas','Courier New',monospace;word-break:break-all;background:var(--bg)}
.log-line{padding:1px 0}
.log-info{color:#cdd9e5}
.log-debug{color:var(--muted)}
.log-success{color:var(--ok)}
.log-error{color:var(--err)}
.log-warn{color:var(--warn)}
/* preview area */
.preview-area{flex-shrink:0;padding:12px;border-top:1px solid var(--border)}
.preview-img{width:100%;height:140px;object-fit:contain;background:#000;border-radius:6px;display:none}
.preview-placeholder{height:140px;display:flex;align-items:center;justify-content:center;color:var(--muted);font-size:12px;border:1px dashed var(--border);border-radius:6px}
.dl-wrap{padding:8px 12px;border-top:1px solid var(--border);flex-shrink:0}
.btn-dl{width:100%;padding:8px;background:#238636;border-radius:var(--r);color:#fff;font-size:13px;font-weight:600;cursor:pointer;border:none;display:none}
.btn-dl:hover{background:#2ea043}
/* scrollbar */
::-webkit-scrollbar{width:5px;height:5px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
</style>
</head>
<body>
<header>
  <h1>🎵 Phigros Render</h1>
  <span>Web UI — server-side rendering</span>
  <a href="/wasm" target="_blank" style="margin-left:auto;color:var(--accent);font-size:12px;text-decoration:none;border:1px solid var(--accent);padding:4px 10px;border-radius:var(--r)">🌐 WASM Player</a>
</header>
<div class="main">

<!-- ── Sidebar: chart list ── -->
<div class="sidebar">
  <input id="search" type="text" placeholder="Search charts…" oninput="filterCharts()">
  <div class="chart-list" id="chartList">
    <div style="padding:16px;color:var(--muted);text-align:center;font-size:12px">Loading charts…</div>
  </div>
</div>

<!-- ── Center: config + launch ── -->
<div class="center">
  <div class="center-top" style="overflow-y:auto;flex:1">
    <div class="selected-chart">
      <img class="selected-thumb" id="selThumb" src="" alt="" onerror="this.style.display='none'">
      <div>
        <div class="selected-title" id="selTitle">Select a chart</div>
        <div class="selected-sub" id="selSub">Choose a chart from the sidebar to begin</div>
      </div>
    </div>

    <div class="tabs">
      <button class="tab active" onclick="switchTab('basic')">Basic</button>
      <button class="tab" onclick="switchTab('effects')">Effects</button>
      <button class="tab" onclick="switchTab('export')">Export</button>
    </div>

    <!-- Basic tab -->
    <div class="tab-panel active" id="tab-basic">
      <div class="cfg-grid">
        <div class="cfg-group">Playback</div>
        <label><span>Width</span><input type="number" id="cfg_width" value="1280" min="320" step="1"></label>
        <label><span>Height</span><input type="number" id="cfg_height" value="720" min="180" step="1"></label>
        <label><span>Duration (s)</span><input type="number" id="cfg_duration" value="" min="0" step="1" placeholder="full chart"></label>
        <label><span>Expand factor</span><input type="number" id="cfg_expand" value="1.0" min="0.1" step="0.1"></label>
        <div class="cfg-group">Preview settings</div>
        <label><span>Preview fps (frames/chart-sec)</span><input type="number" id="cfg_preview_fps" value="1" min="0.1" step="0.1"></label>
      </div>
    </div>

    <!-- Effects tab -->
    <div class="tab-panel" id="tab-effects">
      <div class="toggle-row"><input type="checkbox" id="cfg_trail"><label for="cfg_trail">Trail effect</label></div>
      <div class="toggle-row"><input type="checkbox" id="cfg_motion_blur"><label for="cfg_motion_blur">Motion blur</label></div>
      <div style="margin-top:12px;font-size:11px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.5px">Culling</div>
      <div class="toggle-row"><input type="checkbox" id="cfg_no_cull"><label for="cfg_no_cull">Disable all culling</label></div>
      <div class="toggle-row"><input type="checkbox" id="cfg_no_cull_screen"><label for="cfg_no_cull_screen">Disable screen-bounds culling</label></div>
      <div class="toggle-row"><input type="checkbox" id="cfg_no_cull_enter_time"><label for="cfg_no_cull_enter_time">Disable enter-time culling</label></div>
    </div>

    <!-- Export tab -->
    <div class="tab-panel" id="tab-export">
      <div class="cfg-grid">
        <div class="cfg-group">Video export</div>
        <label><span>Preset</span>
          <select id="cfg_record_preset">
            <option value="fast">fast (CRF 28, ultrafast) ← recommended</option>
            <option value="balanced">balanced (CRF 23, medium)</option>
            <option value="quality">quality (CRF 18, slow)</option>
            <option value="archive">archive (CRF 15, veryslow)</option>
          </select>
        </label>
        <label><span>Codec</span>
          <select id="cfg_record_codec">
            <option value="">libx264 (default)</option>
            <option value="libx265">libx265 (HEVC)</option>
            <option value="libvpx-vp9">libvpx-vp9 (VP9)</option>
          </select>
        </label>
        <label><span>Record FPS</span><input type="number" id="cfg_record_fps" value="30" min="1" step="1"></label>
        <label><span>Resolution (WxH)</span><input type="text" id="cfg_record_resolution" placeholder="default: window size"></label>
      </div>
    </div>

    <div class="render-btns">
      <button class="btn btn-preview" id="btnPreview" onclick="startJob('preview')" disabled>▶ Preview</button>
      <button class="btn btn-export"  id="btnExport"  onclick="startJob('export')"  disabled>⬇ Export MP4</button>
      <button class="btn" id="btnWasm" onclick="openWasmPlayer()" disabled style="background:#7c3aed;color:#fff">🌐 Browser</button>
      <button class="btn btn-stop"    id="btnStop"    onclick="stopJob()"            style="display:none">■ Stop</button>
    </div>
  </div>
</div>

<!-- ── Output panel ── -->
<div class="output">
  <div class="output-header">
    <div class="status-dot" id="statusDot"></div>
    <span id="statusText">Idle</span>
  </div>
  <div class="progress-bar-wrap" id="progressWrap" style="display:none">
    <div class="progress-bar"><div class="progress-fill" id="progressFill" style="width:0%"></div></div>
  </div>
  <div class="log-panel" id="logPanel">
    <div class="log-line log-debug">Logs will appear here when rendering starts.</div>
  </div>
  <div class="preview-area" id="previewArea">
    <div class="preview-placeholder" id="previewPlaceholder">Preview frames will appear here during render</div>
    <img class="preview-img" id="previewImg" alt="preview frame">
  </div>
  <div class="dl-wrap">
    <button class="btn-dl" id="btnDownload" onclick="downloadVideo()">⬇ Download MP4</button>
  </div>
</div>

</div><!-- .main -->

<script>
// ── State ─────────────────────────────────────────────────────────────────────
let charts = [];
let activeChart = null;
let currentJobId = null;
let currentMode = null;
let logSrc = null;    // EventSource for logs
let pollTimer = null; // for preview frame polling

// ── Chart list ────────────────────────────────────────────────────────────────
async function loadCharts() {
  const res = await fetch('/charts');
  charts = await res.json();
  renderChartList(charts);
}

function renderChartList(items) {
  const el = document.getElementById('chartList');
  if (!items.length) {
    el.innerHTML = '<div style="padding:16px;color:var(--muted);text-align:center;font-size:12px">No charts found in charts/</div>';
    return;
  }
  el.innerHTML = items.map((c, idx) => `
    <div class="chart-item" data-idx="${idx}" id="ci_${CSS.escape(c.id)}">
      ${c.bg_url ? `<img class="thumb" src="${c.bg_url}" loading="lazy" onerror="this.style.display='none'">` : '<div class="thumb"></div>'}
      <div class="chart-info">
        <div class="chart-name" title="${c.name}">${c.name}</div>
        ${c.diff ? `<span class="diff-badge ${c.diff}">${c.diff}</span>` : ''}
      </div>
    </div>`).join('');
  // Event delegation: click on chart items
  el.onclick = e => {
    const item = e.target.closest('.chart-item');
    if (!item || !item.dataset.idx) return;
    const idx = parseInt(item.dataset.idx, 10);
    const c = charts.find(ch => ch.id === items[idx]?.id);
    if (c) selectChart(c);
  };
}

function filterCharts() {
  const q = document.getElementById('search').value.toLowerCase();
  renderChartList(q ? charts.filter(c => c.name.toLowerCase().includes(q) || c.diff.toLowerCase().includes(q)) : charts);
}

function selectChart(c) {
  activeChart = c;
  document.querySelectorAll('.chart-item').forEach(el => el.classList.remove('active'));
  const el = document.getElementById('ci_' + CSS.escape(c.id));
  if (el) el.classList.add('active');
  document.getElementById('selTitle').textContent = c.name;
  document.getElementById('selSub').textContent = `Difficulty: ${c.diff || 'unknown'} · ${c.path.split('/').pop()}`;
  const thumb = document.getElementById('selThumb');
  if (c.bg_url) { thumb.src = c.bg_url; thumb.style.display = ''; }
  else thumb.style.display = 'none';
  document.getElementById('btnPreview').disabled = false;
  document.getElementById('btnExport').disabled = false;
  document.getElementById('btnWasm').disabled = false;
}

function openWasmPlayer() {
  if (!activeChart) return;
  // Use rel (relative to charts/) for the chart-data endpoint
  const chartRel = activeChart.rel.replace(/^charts\//, '');
  let url = `/wasm?chart=${encodeURIComponent(chartRel)}`;
  if (activeChart.audio_url) {
    url += `&audio_url=${encodeURIComponent(activeChart.audio_url)}`;
  } else if (activeChart.audio) {
    const audioRel = activeChart.audio.replace(/.*charts\//, '');
    url += `&audio=${encodeURIComponent(audioRel)}`;
  }
  if (activeChart.bg_url) {
    url += `&bg_url=${encodeURIComponent(activeChart.bg_url)}`;
  }
  window.open(url, '_blank');
}

// ── Tabs ──────────────────────────────────────────────────────────────────────
function switchTab(name) {
  document.querySelectorAll('.tab').forEach((t,i) => {
    const tabs = ['basic','effects','export'];
    t.classList.toggle('active', tabs[i] === name);
  });
  document.querySelectorAll('.tab-panel').forEach(p => {
    p.classList.toggle('active', p.id === 'tab-' + name);
  });
}

// ── Config collector ──────────────────────────────────────────────────────────
function getConfig() {
  const n = id => { const v = parseFloat(document.getElementById(id).value); return isNaN(v) ? null : v; };
  const s = id => document.getElementById(id).value || '';
  const b = id => document.getElementById(id).checked;
  return {
    width:   n('cfg_width')  || 1280,
    height:  n('cfg_height') || 720,
    duration: n('cfg_duration'),
    expand:  n('cfg_expand')  || 1.0,
    preview_fps: n('cfg_preview_fps') || 1,
    trail:   b('cfg_trail'),
    motion_blur: b('cfg_motion_blur'),
    no_cull: b('cfg_no_cull'),
    no_cull_screen: b('cfg_no_cull_screen'),
    no_cull_enter_time: b('cfg_no_cull_enter_time'),
    record_preset: s('cfg_record_preset') || 'balanced',
    record_codec:  s('cfg_record_codec'),
    record_fps:    n('cfg_record_fps')   || 60,
    record_resolution: s('cfg_record_resolution'),
  };
}

// ── Job control ───────────────────────────────────────────────────────────────
async function startJob(mode) {
  if (!activeChart) return;
  stopJob();  // cancel any running job first

  clearLog();
  resetPreview();
  document.getElementById('btnDownload').style.display = 'none';
  document.getElementById('btnStop').style.display = 'inline-block';
  document.getElementById('btnPreview').disabled = true;
  document.getElementById('btnExport').disabled = true;
  setStatus('running', mode === 'preview' ? 'Rendering preview…' : 'Exporting MP4…');
  document.getElementById('progressWrap').style.display = 'block';
  document.getElementById('progressFill').style.width = '0%';

  currentMode = mode;

  const res = await fetch('/jobs', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({ mode, chart: activeChart, config: getConfig() }),
  });
  const { id } = await res.json();
  currentJobId = id;

  // SSE log stream
  startLogStream(id);

  // Poll for status + frames
  pollTimer = setInterval(() => pollJob(id), 600);
}

function stopJob() {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
  if (logSrc)    { logSrc.close(); logSrc = null; }
  if (currentJobId) {
    fetch(`/jobs/${currentJobId}`, { method: 'DELETE' }).catch(() => {});
    currentJobId = null;
  }
  document.getElementById('btnStop').style.display = 'none';
  document.getElementById('btnPreview').disabled = !activeChart;
  document.getElementById('btnExport').disabled  = !activeChart;
}

async function pollJob(id) {
  try {
    const res = await fetch(`/jobs/${id}`);
    if (!res.ok) return;
    const data = await res.json();

    // progress bar
    document.getElementById('progressFill').style.width = data.progress + '%';

    // update preview frame
    if (data.n_frames > 0) {
      showFrame(id, data.n_frames - 1);
    }

    if (data.status === 'done') {
      clearInterval(pollTimer); pollTimer = null;
      document.getElementById('btnStop').style.display = 'none';
      document.getElementById('btnPreview').disabled = !activeChart;
      document.getElementById('btnExport').disabled  = !activeChart;
      setStatus('done', 'Done');
      document.getElementById('progressFill').style.width = '100%';
      if (currentMode === 'export' && data.has_output) {
        document.getElementById('btnDownload').style.display = 'block';
      }
    } else if (data.status === 'error' || data.status === 'cancelled') {
      clearInterval(pollTimer); pollTimer = null;
      document.getElementById('btnStop').style.display = 'none';
      document.getElementById('btnPreview').disabled = !activeChart;
      document.getElementById('btnExport').disabled  = !activeChart;
      setStatus('error', data.status === 'cancelled' ? 'Cancelled' : 'Error');
    }
  } catch(e) {}
}

function startLogStream(id) {
  if (logSrc) logSrc.close();
  logSrc = new EventSource(`/jobs/${id}/log`);
  logSrc.onmessage = e => {
    const d = JSON.parse(e.data);
    if (d.done) { logSrc.close(); logSrc = null; return; }
    appendLog(d.msg, d.level);
  };
  logSrc.onerror = () => { logSrc.close(); logSrc = null; };
}

// ── Log helpers ───────────────────────────────────────────────────────────────
function appendLog(msg, level) {
  const panel = document.getElementById('logPanel');
  const div = document.createElement('div');
  div.className = `log-line log-${level || 'info'}`;
  div.textContent = msg;
  panel.appendChild(div);
  panel.scrollTop = panel.scrollHeight;
}
function clearLog() {
  document.getElementById('logPanel').innerHTML = '';
}

// ── Preview helpers ───────────────────────────────────────────────────────────
let _shownFrame = -1;
function showFrame(jobId, idx) {
  if (idx === _shownFrame) return;
  _shownFrame = idx;
  const img = document.getElementById('previewImg');
  const ph  = document.getElementById('previewPlaceholder');
  img.src = `/jobs/${jobId}/frame/${idx}?t=${Date.now()}`;
  img.style.display = 'block';
  ph.style.display = 'none';
}
function resetPreview() {
  _shownFrame = -1;
  document.getElementById('previewImg').style.display = 'none';
  document.getElementById('previewPlaceholder').style.display = 'flex';
}

// ── Status helpers ────────────────────────────────────────────────────────────
function setStatus(state, text) {
  document.getElementById('statusDot').className = `status-dot ${state}`;
  document.getElementById('statusText').textContent = text;
}

// ── Download ──────────────────────────────────────────────────────────────────
function downloadVideo() {
  if (currentJobId) window.location.href = `/jobs/${currentJobId}/download`;
}

// ── Init ──────────────────────────────────────────────────────────────────────
loadCharts();
</script>
</body>
</html>
"""

# ── CLI entry point ────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Phigros Render Web UI")
    parser.add_argument("--port",   type=int,  default=5000, help="Port (default 5000)")
    parser.add_argument("--host",   default="0.0.0.0",       help="Bind host")
    parser.add_argument("--binary", default=str(_BIN),        help="Path to phigros_render binary")
    parser.add_argument("--debug",  action="store_true")
    a = parser.parse_args()

    if not Path(a.binary).exists():
        print(f"[warn] Binary not found: {a.binary}")
        print("  Build with: cd cpp/build && cmake .. && make -j$(nproc)")
    else:
        _BIN = Path(a.binary)
        print(f"[info] Binary: {_BIN}")

    if not _CHARTS.exists():
        print(f"[warn] Charts dir not found: {_CHARTS}")
    else:
        n = sum(1 for _ in _CHARTS.glob("*/*.json"))
        print(f"[info] Charts: {n} found in {_CHARTS}")

    print(f"[info] Open http://localhost:{a.port}")
    app.run(host=a.host, port=a.port, debug=a.debug, threaded=True)
