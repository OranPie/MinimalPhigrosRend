#!/usr/bin/env python3
"""
phigros_render TUI Launcher
Usage:  python3 cpp/scripts/launcher.py [--binary PATH] [--charts DIR]
Requires: textual >= 0.60  (pip install textual)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

# ── locate project root & default paths ───────────────────────────────────────
_SCRIPT_DIR = Path(__file__).resolve().parent          # cpp/scripts/
_CPP_DIR    = _SCRIPT_DIR.parent                       # cpp/
_ROOT       = _CPP_DIR.parent                          # repo root

DEFAULT_BINARY = str(_CPP_DIR / "build" / "phigros_render")
DEFAULT_CHARTS = str(_ROOT / "charts")
DEFAULT_RESPACK = str(_ROOT / "respack.zip")

# ── Textual imports ────────────────────────────────────────────────────────────
try:
    from textual.app import App, ComposeResult
    from textual.binding import Binding
    from textual.containers import Container, Horizontal, Vertical, ScrollableContainer
    from textual.screen import ModalScreen
    from textual.widgets import (
        Button, Checkbox, Footer, Header, Input, Label,
        ListItem, ListView, Select, Static, TabbedContent, TabPane,
    )
except ImportError:
    sys.exit("textual not found — run:  pip install textual")

# ─────────────────────────────────────────────────────────────────────────────
# Chart discovery
# ─────────────────────────────────────────────────────────────────────────────

DIFF_LABEL = {"EZ": "EZ", "HD": "HD", "IN": "IN", "AT": "AT",
              "ez": "EZ", "hd": "HD", "in": "IN", "at": "AT"}

def _find_charts(charts_dir: str) -> list[dict]:
    """Return list of {name, diff, path, audio, bg} dicts."""
    results: list[dict] = []
    root = Path(charts_dir)
    if not root.is_dir():
        return results
    # Direct chart JSONs in a named folder
    for folder in sorted(root.iterdir()):
        if not folder.is_dir():
            continue
        for jf in sorted(folder.glob("*.json")):
            stem = jf.stem.upper()
            diff = DIFF_LABEL.get(stem, jf.stem)
            # infer audio / bg
            audio = next(iter(folder.glob("*.ogg")), None) or \
                    next(iter(folder.glob("*.mp3")), None)
            bg    = next(iter(folder.glob("*.png")), None) or \
                    next(iter(folder.glob("*.jpg")), None)
            results.append({
                "name": folder.name,
                "diff": diff,
                "path": str(jf),
                "audio": str(audio) if audio else "",
                "bg":    str(bg)    if bg    else "",
                "label": f"{folder.name}  [{diff}]",
            })
    # Also catch zip-packed charts at root
    for zf in sorted(root.glob("*.zip")):
        results.append({
            "name": zf.stem,
            "diff": "",
            "path": str(zf),
            "audio": "",
            "bg": "",
            "label": f"📦 {zf.stem}",
        })
    return results

# ─────────────────────────────────────────────────────────────────────────────
# CLI builder
# ─────────────────────────────────────────────────────────────────────────────

def build_cmd(binary: str, chart_path: str, cfg: dict, extra: dict) -> list[str]:
    cmd = [binary, chart_path]
    if cfg.get("respack"):      cmd += ["--respack",      cfg["respack"]]
    if extra.get("audio"):      cmd += ["--audio",        extra["audio"]]
    if extra.get("bg"):         cmd += ["--bg",           extra["bg"]]
    if cfg.get("config_file"):  cmd += ["--config",       cfg["config_file"]]
    if cfg.get("width")  != 1280: cmd += ["--width",  str(cfg["width"])]
    if cfg.get("height") != 720:  cmd += ["--height", str(cfg["height"])]
    if cfg.get("audio_offset") != 0.0:
        cmd += ["--audio-offset", str(cfg["audio_offset"])]
    if cfg.get("duration", 0.0) > 0:
        cmd += ["--duration", str(cfg["duration"])]
    if cfg.get("headless"):     cmd.append("--headless")
    if cfg.get("score_only"):   cmd.append("--score-only")
    if cfg.get("play_mode"):    cmd.append("--play")
    return cmd

def cfg_to_json(cfg: dict) -> dict:
    """Convert launcher cfg dict to phigros_render JSON config."""
    d: dict = {}
    d["w"]  = cfg.get("width",  1280)
    d["h"]  = cfg.get("height", 720)
    for k in ("approach", "chart_speed", "expand", "note_scale_x",
              "note_scale_y", "audio_offset_ms", "bg_blur", "bg_dim",
              "autoplay", "basic_debug", "show_hitfx", "show_particles",
              "no_cull", "no_cull_screen", "no_cull_enter_time",
              "note_outline", "hitfx_intensity", "particle_count",
              "trail_frames", "trail_decay", "trail_blur",
              "trail_dim", "trail_blur_ramp",
              "motion_blur_shutter"):
        if k in cfg and cfg[k] is not None:
            d[k] = cfg[k]
    if cfg.get("trail_alpha", 0.0) > 0:
        d["trail_alpha"] = cfg["trail_alpha"]
    if cfg.get("motion_blur_samples", 0) > 0:
        d["motion_blur_samples"] = cfg["motion_blur_samples"]
    if cfg.get("respack"):
        d["respack"] = cfg["respack"]
    return d

# ─────────────────────────────────────────────────────────────────────────────
# Confirm / run screen
# ─────────────────────────────────────────────────────────────────────────────

class ConfirmScreen(ModalScreen[bool]):
    BINDINGS = [Binding("escape", "dismiss(False)", "Cancel")]

    def __init__(self, cmd: list[str]) -> None:
        super().__init__()
        self._cmd = cmd

    def compose(self) -> ComposeResult:
        cmdstr = " ".join(shlex.quote(c) for c in self._cmd)
        with Container(id="confirm-box"):
            yield Label("Launch command", id="confirm-title")
            yield Static(cmdstr, id="confirm-cmd")
            with Horizontal(id="confirm-buttons"):
                yield Button("▶  Launch", variant="success", id="btn-launch")
                yield Button("  Cancel",  variant="error",   id="btn-cancel")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(event.button.id == "btn-launch")

# ─────────────────────────────────────────────────────────────────────────────
# Main App
# ─────────────────────────────────────────────────────────────────────────────

class PhigrosLauncher(App):
    CSS = """
    Screen {
        background: $surface;
    }

    /* ── layout ── */
    #main-row {
        height: 1fr;
    }
    #left-panel {
        width: 36;
        border: solid $primary;
        margin: 0 1 0 0;
    }
    #left-panel Label {
        background: $primary;
        color: $text;
        width: 100%;
        padding: 0 1;
    }
    #chart-list {
        height: 1fr;
    }
    #right-panel {
        width: 1fr;
        border: solid $primary;
    }
    #cmd-bar {
        height: 5;
        border-top: solid $primary;
        padding: 0 1;
        background: $panel;
    }
    #cmd-label {
        color: $text-muted;
        height: 1;
    }
    #cmd-preview {
        height: 3;
        color: $success;
        overflow: hidden;
    }
    #launch-row {
        height: 3;
        align-horizontal: right;
        padding: 0 1;
        border-top: solid $primary;
    }
    Button {
        margin: 0 1;
    }

    /* ── config tabs ── */
    TabPane {
        padding: 1 2;
    }
    .field-row {
        height: 3;
        margin-bottom: 1;
    }
    .field-label {
        width: 26;
        padding: 1 0;
        color: $text-muted;
    }
    .field-input {
        width: 16;
    }
    .wide-input {
        width: 40;
    }
    Checkbox {
        margin-top: 1;
    }

    /* ── confirm modal ── */
    #confirm-box {
        width: 80;
        height: auto;
        border: double $primary;
        background: $surface;
        padding: 1 2;
        align: center middle;
    }
    #confirm-title {
        text-style: bold;
        margin-bottom: 1;
    }
    #confirm-cmd {
        background: $panel;
        border: solid $accent;
        padding: 0 1;
        height: auto;
        margin-bottom: 1;
        color: $success;
    }
    #confirm-buttons {
        align-horizontal: center;
        height: 3;
    }
    """

    BINDINGS = [
        Binding("ctrl+q", "quit",   "Quit"),
        Binding("f5",     "launch", "Launch"),
        Binding("ctrl+s", "save_config", "Save config"),
    ]

    TITLE = "Phigros Render — Launcher"
    SUB_TITLE = "TUI config picker & chart launcher"

    def __init__(self, binary: str, charts_dir: str) -> None:
        super().__init__()
        self._binary    = binary
        self._charts    = _find_charts(charts_dir)
        self._selected: dict | None = None
        # default config values
        self._cfg = {
            "width": 1280, "height": 720,
            "approach": 3.0, "chart_speed": 1.0, "expand": 1.0,
            "note_scale_x": 2.5, "note_scale_y": 1.0,
            "audio_offset": 0.0, "duration": 0.0,
            "autoplay": True,  "headless": False,
            "score_only": False, "play_mode": False,
            "basic_debug": False,
            "show_hitfx": True, "show_particles": True,
            "hitfx_intensity": 1.0, "particle_count": 8,
            "no_cull": False, "no_cull_screen": False,
            "no_cull_enter_time": True,
            "note_outline": False,
            "trail_alpha": 0.0, "trail_frames": 6,
            "trail_decay": 0.75, "trail_blur": 0, "trail_dim": 0,
            "trail_blur_ramp": False,
            "motion_blur_samples": 0, "motion_blur_shutter": 0.5,
            "bg_blur": 10, "bg_dim": 120,
            "respack": DEFAULT_RESPACK,
            "config_file": "",
        }

    # ── compose ───────────────────────────────────────────────────────────────
    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal(id="main-row"):
            # Left: chart list
            with Vertical(id="left-panel"):
                yield Label("📂 Charts")
                yield ListView(
                    *[ListItem(Label(c["label"])) for c in self._charts],
                    id="chart-list",
                )
            # Right: config tabs
            with Vertical(id="right-panel"):
                with TabbedContent():
                    with TabPane("Basic", id="tab-basic"):
                        yield from self._basic_fields()
                    with TabPane("Effects", id="tab-effects"):
                        yield from self._effects_fields()
                    with TabPane("Trail / MB", id="tab-trail"):
                        yield from self._trail_fields()
                    with TabPane("Assets", id="tab-assets"):
                        yield from self._assets_fields()

        # Bottom: cmd preview
        with Container(id="cmd-bar"):
            yield Label("CLI preview", id="cmd-label")
            yield Static("(select a chart first)", id="cmd-preview")
        with Horizontal(id="launch-row"):
            yield Button("💾 Save config JSON", id="btn-save",  variant="default")
            yield Button("▶  Launch  [F5]",    id="btn-launch", variant="success")

        yield Footer()

    # ── field helpers ─────────────────────────────────────────────────────────
    def _row(self, label: str, widget, extra_cls: str = "") -> ComposeResult:
        with Horizontal(classes=f"field-row {extra_cls}"):
            yield Label(label, classes="field-label")
            yield widget

    def _basic_fields(self) -> ComposeResult:
        c = self._cfg
        yield from self._row("Width",        Input(str(c["width"]),       id="f-width",       classes="field-input"))
        yield from self._row("Height",        Input(str(c["height"]),      id="f-height",      classes="field-input"))
        yield from self._row("Approach (s)",  Input(str(c["approach"]),    id="f-approach",    classes="field-input"))
        yield from self._row("Chart speed",   Input(str(c["chart_speed"]), id="f-chart_speed", classes="field-input"))
        yield from self._row("Expand",        Input(str(c["expand"]),      id="f-expand",      classes="field-input"))
        yield from self._row("Note scale X",  Input(str(c["note_scale_x"]),id="f-note_scale_x",classes="field-input"))
        yield from self._row("Audio offset ms", Input(str(c["audio_offset"]), id="f-audio_offset", classes="field-input"))
        yield from self._row("Duration (0=full)", Input(str(c["duration"]),id="f-duration",    classes="field-input"))
        yield Checkbox("Autoplay",    value=c["autoplay"],    id="f-autoplay")
        yield Checkbox("Play mode",   value=c["play_mode"],   id="f-play_mode")
        yield Checkbox("Headless",    value=c["headless"],    id="f-headless")
        yield Checkbox("Score only",  value=c["score_only"],  id="f-score_only")
        yield Checkbox("Debug overlay", value=c["basic_debug"], id="f-basic_debug")

    def _effects_fields(self) -> ComposeResult:
        c = self._cfg
        yield Checkbox("Show hit effects",  value=c["show_hitfx"],      id="f-show_hitfx")
        yield Checkbox("Show particles",    value=c["show_particles"],   id="f-show_particles")
        yield Checkbox("Note outline",      value=c["note_outline"],     id="f-note_outline")
        yield Checkbox("Disable all culling",   value=c["no_cull"],         id="f-no_cull")
        yield Checkbox("Disable screen culling",value=c["no_cull_screen"],  id="f-no_cull_screen")
        yield Checkbox("Enable t_enter cull",   value=not c["no_cull_enter_time"], id="f-cull_enter")
        yield from self._row("Hit FX intensity", Input(str(c["hitfx_intensity"]),id="f-hitfx_intensity",classes="field-input"))
        yield from self._row("Particles/hit",   Input(str(c["particle_count"]), id="f-particle_count",  classes="field-input"))
        yield from self._row("BG blur",         Input(str(c["bg_blur"]),        id="f-bg_blur",         classes="field-input"))
        yield from self._row("BG dim (0-255)",  Input(str(c["bg_dim"]),         id="f-bg_dim",          classes="field-input"))

    def _trail_fields(self) -> ComposeResult:
        c = self._cfg
        yield from self._row("Trail alpha (0=off)", Input(str(c["trail_alpha"]),   id="f-trail_alpha",   classes="field-input"))
        yield from self._row("Trail frames",         Input(str(c["trail_frames"]),  id="f-trail_frames",  classes="field-input"))
        yield from self._row("Trail decay",          Input(str(c["trail_decay"]),   id="f-trail_decay",   classes="field-input"))
        yield from self._row("Trail blur",           Input(str(c["trail_blur"]),    id="f-trail_blur",    classes="field-input"))
        yield from self._row("Trail dim (0-255)",    Input(str(c["trail_dim"]),     id="f-trail_dim",     classes="field-input"))
        yield Checkbox("Trail blur ramp", value=c["trail_blur_ramp"], id="f-trail_blur_ramp")
        yield from self._row("MB samples (0=off)",   Input(str(c["motion_blur_samples"]),  id="f-motion_blur_samples",  classes="field-input"))
        yield from self._row("MB shutter (0-1)",     Input(str(c["motion_blur_shutter"]),  id="f-motion_blur_shutter",  classes="field-input"))

    def _assets_fields(self) -> ComposeResult:
        c = self._cfg
        yield from self._row("Binary",   Input(self._binary,       id="f-binary",      classes="wide-input"))
        yield from self._row("Respack",  Input(c["respack"],        id="f-respack",     classes="wide-input"))
        yield from self._row("Config JSON", Input(c["config_file"],  id="f-config_file", classes="wide-input"))

    # ── reactive helpers ──────────────────────────────────────────────────────
    def _sync_cfg(self) -> None:
        """Read all Input/Checkbox widgets back into self._cfg."""
        def gf(id_: str, default):
            try:
                w = self.query_one(f"#{id_}")
                if isinstance(w, Input):
                    t = w.value.strip()
                    if isinstance(default, int):   return int(float(t))   if t else default
                    if isinstance(default, float): return float(t)        if t else default
                    return t or default
                if isinstance(w, Checkbox):
                    return w.value
            except Exception:
                pass
            return default
        c = self._cfg
        c["width"]         = gf("f-width",      1280)
        c["height"]        = gf("f-height",     720)
        c["approach"]      = gf("f-approach",   3.0)
        c["chart_speed"]   = gf("f-chart_speed",1.0)
        c["expand"]        = gf("f-expand",     1.0)
        c["note_scale_x"]  = gf("f-note_scale_x",2.5)
        c["audio_offset"]  = gf("f-audio_offset",0.0)
        c["duration"]      = gf("f-duration",   0.0)
        c["autoplay"]      = gf("f-autoplay",   True)
        c["play_mode"]     = gf("f-play_mode",  False)
        c["headless"]      = gf("f-headless",   False)
        c["score_only"]    = gf("f-score_only", False)
        c["basic_debug"]   = gf("f-basic_debug",False)
        c["show_hitfx"]    = gf("f-show_hitfx", True)
        c["show_particles"]= gf("f-show_particles",True)
        c["note_outline"]  = gf("f-note_outline",False)
        c["no_cull"]       = gf("f-no_cull",    False)
        c["no_cull_screen"]= gf("f-no_cull_screen",False)
        # invert: checkbox "Enable t_enter cull" = True means no_cull_enter_time = False
        c["no_cull_enter_time"] = not gf("f-cull_enter", False)
        c["hitfx_intensity"]    = gf("f-hitfx_intensity",1.0)
        c["particle_count"]     = gf("f-particle_count", 8)
        c["bg_blur"]       = gf("f-bg_blur",    10)
        c["bg_dim"]        = gf("f-bg_dim",     120)
        c["trail_alpha"]   = gf("f-trail_alpha",0.0)
        c["trail_frames"]  = gf("f-trail_frames",6)
        c["trail_decay"]   = gf("f-trail_decay",0.75)
        c["trail_blur"]    = gf("f-trail_blur", 0)
        c["trail_dim"]     = gf("f-trail_dim",  0)
        c["trail_blur_ramp"]= gf("f-trail_blur_ramp",False)
        c["motion_blur_samples"] = gf("f-motion_blur_samples",0)
        c["motion_blur_shutter"] = gf("f-motion_blur_shutter",0.5)
        c["respack"]       = gf("f-respack",    DEFAULT_RESPACK)
        c["config_file"]   = gf("f-config_file","")
        try:
            self._binary = self.query_one("#f-binary", Input).value.strip() or self._binary
        except Exception:
            pass

    def _update_preview(self) -> None:
        self._sync_cfg()
        try:
            preview = self.query_one("#cmd-preview", Static)
        except Exception:
            return
        if not self._selected:
            preview.update("(select a chart from the left panel)")
            return
        cmd = build_cmd(self._binary, self._selected["path"],
                        self._cfg, self._selected)
        preview.update(" ".join(shlex.quote(c) for c in cmd))

    # ── event handlers ────────────────────────────────────────────────────────
    def on_list_view_selected(self, event: ListView.Selected) -> None:
        idx = event.list_view._nodes.index(event.item)
        if 0 <= idx < len(self._charts):
            self._selected = self._charts[idx]
        self._update_preview()

    def on_input_changed(self, _: Input.Changed) -> None:
        self._update_preview()

    def on_checkbox_changed(self, _: Checkbox.Changed) -> None:
        self._update_preview()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "btn-launch":
            self.action_launch()
        elif event.button.id == "btn-save":
            self.action_save_config()

    # ── actions ───────────────────────────────────────────────────────────────
    def action_launch(self) -> None:
        self._sync_cfg()
        if not self._selected:
            self.notify("Select a chart first!", severity="warning")
            return
        cmd = build_cmd(self._binary, self._selected["path"],
                        self._cfg, self._selected)

        async def _run_confirm(launch: bool) -> None:
            if not launch:
                return
            self.exit(cmd)   # return cmd to __main__ and run there

        self.push_screen(ConfirmScreen(cmd), _run_confirm)

    def action_save_config(self) -> None:
        self._sync_cfg()
        out_path = Path(_ROOT) / "config" / "launcher_config.json"
        data = cfg_to_json(self._cfg)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "w") as f:
            json.dump(data, f, indent=2)
        self.notify(f"Saved → {out_path}", severity="information")

    def action_quit(self) -> None:
        self.exit(None)


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Phigros Render TUI Launcher")
    parser.add_argument("--binary", default=DEFAULT_BINARY,
                        help=f"Path to phigros_render binary (default: {DEFAULT_BINARY})")
    parser.add_argument("--charts", default=DEFAULT_CHARTS,
                        help=f"Charts directory (default: {DEFAULT_CHARTS})")
    args = parser.parse_args()

    if not Path(args.binary).exists():
        print(f"[warn] Binary not found: {args.binary}")
        print("       Build with:  cd cpp/build && cmake .. && make -j$(nproc) phigros_render")

    app = PhigrosLauncher(binary=args.binary, charts_dir=args.charts)
    cmd = app.run()   # returns the launch cmd list (or None if quit)

    if cmd:
        print("\n[Launcher] Running:", " ".join(shlex.quote(c) for c in cmd))
        try:
            subprocess.run(cmd, check=False)
        except FileNotFoundError as e:
            print(f"[error] {e}")
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
