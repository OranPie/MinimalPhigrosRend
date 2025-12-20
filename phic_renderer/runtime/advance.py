from __future__ import annotations

import os
import json
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ..types import RuntimeLine, RuntimeNote
from ..io.chart_pack_impl import ChartPack, load_chart_pack
from ..io.chart_loader_impl import load_chart
from .timewarp import _TimeWarpEval, _TimeWarpIntegral
from ..math.util import clamp


@dataclass
class AdvanceLoadResult:
    fmt: str
    offset: float
    lines: List[RuntimeLine]
    notes: List[RuntimeNote]

    advance_active: bool
    advance_cfg: Optional[Dict[str, Any]]
    advance_mix: bool
    advance_tracks_bgm: List[Dict[str, Any]]
    advance_main_bgm: Optional[str]
    advance_segment_starts: List[float]
    advance_segment_bgm: List[Optional[str]]
    advance_base_dir: Optional[str]
    advance_mods: Optional[Dict[str, Any]]

    chart_path: Optional[str]
    chart_info: Dict[str, Any]
    bg_path: Optional[str]
    music_path: Optional[str]
    bg_dim_alpha: Optional[int]

    packs_keepalive: List[ChartPack]


def _pick_first_existing(base_dir: str, candidates: List[str]) -> Optional[str]:
    for fn in candidates:
        p = os.path.join(base_dir, fn)
        if os.path.exists(p):
            return p
    return None


def _auto_pick_asset_by_basename(base_dir: str, base_name: str, exts: Tuple[str, ...]) -> Optional[str]:
    try:
        items = os.listdir(base_dir)
    except Exception:
        return None

    base_lower = str(base_name).lower()
    for fn in items:
        try:
            if not fn.lower().endswith(exts):
                continue
            stem, _ = os.path.splitext(fn)
            if stem.lower() == base_lower:
                return os.path.join(base_dir, fn)
        except Exception:
            continue
    return None


def _resolve_loose_chart_dir(dir_path: str, diff: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """Resolve a local folder chart that does NOT have pack info.yml.

    Expected layout:
    - IN.json / AT.json (also allow in.json / at.json)
    - bg / bgm files in the same folder
    """

    diff_u = str(diff or "IN").strip().upper()
    if diff_u not in {"IN", "AT"}:
        diff_u = "IN"

    chart_p = _pick_first_existing(dir_path, [f"{diff_u}.json", f"{diff_u.lower()}.json"])

    # Heuristics for assets
    folder_name = os.path.basename(os.path.abspath(dir_path))

    bg_exts = (".png", ".jpg", ".jpeg", ".webp")
    bg_p = _pick_first_existing(dir_path, [
        "illustration.png",
        "illustration.jpg",
        "illustration.jpeg",
        "illustration.webp",
        "background.png",
        "background.jpg",
        "background.jpeg",
        "background.webp",
        "bg.png",
        "bg.jpg",
        "bg.jpeg",
        "bg.webp",
    ])
    if not bg_p:
        bg_p = _auto_pick_asset_by_basename(dir_path, folder_name, bg_exts)

    audio_exts = (".ogg", ".mp3", ".wav")
    music_p = _pick_first_existing(dir_path, [
        "song.ogg",
        "song.mp3",
        "song.wav",
        "music.ogg",
        "music.mp3",
        "music.wav",
        "bgm.ogg",
        "bgm.mp3",
        "bgm.wav",
    ])
    if not music_p:
        music_p = _auto_pick_asset_by_basename(dir_path, folder_name, audio_exts)

    return chart_p, music_p, bg_p


def _list_loose_chart_files(dir_path: str) -> List[str]:
    try:
        items = os.listdir(dir_path)
    except Exception:
        return []

    out: List[str] = []
    for fn in items:
        try:
            if not fn.lower().endswith(".json"):
                continue
            if fn.lower() in {"info.json", "meta.json"}:
                continue
            out.append(os.path.join(dir_path, fn))
        except Exception:
            continue

    def _key(p: str) -> Tuple[int, str]:
        b = os.path.basename(p).lower()
        if b in {"in.json", "at.json"}:
            return (0, b)
        return (1, b)

    out.sort(key=_key)
    return out


def _build_advance_cfg_from_dir(dir_path: str) -> Tuple[Dict[str, Any], Optional[str], Optional[str]]:
    charts = _list_loose_chart_files(dir_path)
    if not charts:
        raise SystemExit(f"No chart json found in folder: {dir_path}")

    # Use directory-level heuristics for shared assets.
    _chart_p, music_p, bg_p = _resolve_loose_chart_dir(dir_path, "IN")

    tracks: List[Dict[str, Any]] = []
    for p in charts:
        tracks.append({
            "input": p,
            "start_at": 0.0,
        })

    cfg: Dict[str, Any] = {
        "mode": "tracks",
        "main": 0,
        "tracks": tracks,
    }

    # Optionally attach shared bg/bgms to the main track so the existing loader picks them up.
    if music_p:
        try:
            cfg["tracks"][0]["bgm"] = music_p
        except Exception:
            pass
    if bg_p:
        try:
            cfg["tracks"][0]["bg"] = bg_p
        except Exception:
            pass

    return cfg, music_p, bg_p


def load_from_args(args: Any, W: int, H: int) -> AdvanceLoadResult:
    chart_path: Optional[str] = getattr(args, "input", None)
    pack: Optional[ChartPack] = None

    music_path = None
    bg_path = None
    chart_info: Dict[str, Any] = {}
    bg_dim_alpha: Optional[int] = None

    advance_cfg: Optional[Dict[str, Any]] = None
    advance_active = False
    advance_mix = False
    advance_tracks_bgm: List[Dict[str, Any]] = []
    advance_main_bgm: Optional[str] = None
    advance_segment_starts: List[float] = []
    advance_segment_bgm: List[Optional[str]] = []
    advance_base_dir: Optional[str] = None
    advance_mods: Optional[Dict[str, Any]] = None

    packs_keepalive: List[ChartPack] = []

    auto_advance_base_dir: Optional[str] = None
    auto_advance_cfg: Optional[Dict[str, Any]] = None
    if chart_path and os.path.isdir(chart_path) and (not os.path.exists(os.path.join(str(chart_path), "info.yml"))):
        # Loose folder charts are not allowed to be loaded as a single chart.
        # Instead, automatically build an advance config from all json charts in the folder.
        auto_advance_base_dir = os.path.abspath(str(chart_path))
        auto_advance_cfg, music_path, bg_path = _build_advance_cfg_from_dir(str(chart_path))
        chart_info = {}

    if getattr(args, "advance", None) or auto_advance_cfg is not None:
        if auto_advance_cfg is not None:
            advance_cfg = auto_advance_cfg
            advance_base_dir = auto_advance_base_dir
        else:
            with open(str(args.advance), "r", encoding="utf-8") as f:
                advance_cfg = json.load(f) or {}
            advance_base_dir = os.path.dirname(os.path.abspath(str(args.advance)))
        advance_active = True
        advance_mix = bool(advance_cfg.get("mix", False))
        advance_mods = advance_cfg.get("mods") if isinstance(advance_cfg, dict) else None

        mode = str(advance_cfg.get("mode", "sequence"))
        all_lines: List[RuntimeLine] = []
        all_notes: List[RuntimeNote] = []
        lid_base = 0

        def _load_one_input(inp: str) -> Tuple[str, float, List[RuntimeLine], List[RuntimeNote], Optional[str], Optional[str], Dict[str, Any], str, str]:
            p = None
            chart_p = inp
            music_p = None
            bg_p = None
            info = {}
            if os.path.isdir(inp) or (os.path.isfile(inp) and str(inp).lower().endswith((".zip", ".pez"))):
                p = load_chart_pack(inp)
                packs_keepalive.append(p)
                chart_p = p.chart_path
                music_p = p.music_path
                bg_p = p.bg_path
                info = p.info
            fmt_i, off_i, lines_i, notes_i = load_chart(chart_p, W, H)
            base_dir = os.path.dirname(os.path.abspath(chart_p))
            return fmt_i, off_i, lines_i, notes_i, music_p, bg_p, info, chart_p, base_dir

        if mode == "sequence":
            items = list(advance_cfg.get("items", []) or [])
            cur_start = 0.0
            for it in items:
                inp = str(it.get("input"))
                start_local = float(it.get("start", 0.0))
                end_local = it.get("end", None)
                end_local = float(end_local) if end_local is not None else 1e18
                speed = float(it.get("chart_speed", 1.0))
                start_at = float(it.get("start_at", cur_start))
                fmt_i, off_i, lines_i, notes_i, music_p, bg_p, info, chart_p, base_dir = _load_one_input(inp)

                seg_bgm = str(it.get("bgm")) if it.get("bgm") else (music_p if (music_p and os.path.exists(music_p)) else None)
                if seg_bgm and (not os.path.isabs(seg_bgm)):
                    seg_bgm = os.path.join(base_dir, seg_bgm)
                seg_bg = str(it.get("bg")) if it.get("bg") else (bg_p if (bg_p and os.path.exists(bg_p)) else None)
                if seg_bg and (not os.path.isabs(seg_bg)):
                    seg_bg = os.path.join(base_dir, seg_bg)
                if seg_bg and (not bg_path):
                    bg_path = seg_bg
                    chart_info = info or {}
                    bd = chart_info.get("backgroundDim", None)
                    if bd is not None:
                        bg_dim_alpha = int(clamp(float(bd), 0.0, 1.0) * 255)

                time_offset = float(it.get("time_offset", 0.0)) + start_local + off_i

                lid_map: Dict[int, int] = {}
                for ln in lines_i:
                    new_lid = lid_base
                    lid_base += 1
                    lid_map[ln.lid] = new_lid
                    all_lines.append(RuntimeLine(
                        lid=new_lid,
                        pos_x=_TimeWarpEval(ln.pos_x, start_at, speed, off_i, time_offset),
                        pos_y=_TimeWarpEval(ln.pos_y, start_at, speed, off_i, time_offset),
                        rot=_TimeWarpEval(ln.rot, start_at, speed, off_i, time_offset),
                        alpha=_TimeWarpEval(ln.alpha, start_at, speed, off_i, time_offset),
                        scroll_px=_TimeWarpIntegral(ln.scroll_px, start_at, speed, off_i, time_offset),
                        color_rgb=ln.color_rgb,
                        name=ln.name,
                        event_counts=ln.event_counts,
                    ))

                for n in notes_i:
                    if n.fake:
                        continue
                    if n.t_hit < start_local or n.t_hit > end_local:
                        continue
                    t_hit_m = start_at + (n.t_hit + off_i - time_offset) / max(1e-9, speed)
                    t_end_local = min(n.t_end, end_local)
                    t_end_m = start_at + (t_end_local + off_i - time_offset) / max(1e-9, speed)
                    new_line = lid_map.get(n.line_id, n.line_id)

                    hs = n.hitsound_path
                    if hs:
                        hs_abs = os.path.join(base_dir, hs)
                        if os.path.exists(hs_abs):
                            hs = hs_abs

                    nn = RuntimeNote(
                        nid=n.nid,
                        line_id=new_line,
                        kind=n.kind,
                        above=n.above,
                        fake=False,
                        t_hit=t_hit_m,
                        t_end=t_end_m,
                        x_local_px=n.x_local_px,
                        y_offset_px=n.y_offset_px,
                        speed_mul=n.speed_mul,
                        size_px=n.size_px,
                        alpha01=n.alpha01,
                        hitsound_path=hs,
                        t_enter=n.t_enter,
                        mh=n.mh,
                    )
                    all_notes.append(nn)

                seg_dur = max(0.0, (end_local - start_local) / max(1e-9, speed))
                advance_segment_starts.append(start_at)
                advance_segment_bgm.append(seg_bgm)
                cur_start = max(cur_start, start_at + seg_dur)

            fmt = "advance"
            offset = 0.0
            lines = all_lines
            notes = sorted(all_notes, key=lambda x: x.t_hit)
            advance_main_bgm = advance_segment_bgm[0] if advance_segment_bgm else None

        else:
            tracks = list(advance_cfg.get("tracks", []) or [])
            main_idx = int(advance_cfg.get("main", 0) or 0)
            for idx, tr in enumerate(tracks):
                inp = str(tr.get("input"))
                start_at = float(tr.get("start_at", 0.0))
                end_at = tr.get("end_at", None)
                end_at = float(end_at) if end_at is not None else None
                speed = float(tr.get("chart_speed", 1.0))
                fmt_i, off_i, lines_i, notes_i, music_p, bg_p, info, chart_p, base_dir = _load_one_input(inp)

                time_offset = float(tr.get("time_offset", 0.0)) + off_i
                lid_map: Dict[int, int] = {}
                for ln in lines_i:
                    new_lid = lid_base
                    lid_base += 1
                    lid_map[ln.lid] = new_lid
                    all_lines.append(RuntimeLine(
                        lid=new_lid,
                        pos_x=_TimeWarpEval(ln.pos_x, start_at, speed, off_i, time_offset),
                        pos_y=_TimeWarpEval(ln.pos_y, start_at, speed, off_i, time_offset),
                        rot=_TimeWarpEval(ln.rot, start_at, speed, off_i, time_offset),
                        alpha=_TimeWarpEval(ln.alpha, start_at, speed, off_i, time_offset),
                        scroll_px=_TimeWarpIntegral(ln.scroll_px, start_at, speed, off_i, time_offset),
                        color_rgb=ln.color_rgb,
                        name=ln.name,
                        event_counts=ln.event_counts,
                    ))

                for n in notes_i:
                    if n.fake:
                        continue
                    t_hit_m = start_at + (n.t_hit + off_i - time_offset) / max(1e-9, speed)
                    t_end_m = start_at + (n.t_end + off_i - time_offset) / max(1e-9, speed)
                    if end_at is not None and t_hit_m > end_at:
                        continue
                    if end_at is not None:
                        t_end_m = min(t_end_m, end_at)

                    new_line = lid_map.get(n.line_id, n.line_id)

                    hs = n.hitsound_path
                    if hs:
                        hs_abs = os.path.join(base_dir, hs)
                        if os.path.exists(hs_abs):
                            hs = hs_abs

                    nn = RuntimeNote(
                        nid=n.nid,
                        line_id=new_line,
                        kind=n.kind,
                        above=n.above,
                        fake=False,
                        t_hit=t_hit_m,
                        t_end=t_end_m,
                        x_local_px=n.x_local_px,
                        y_offset_px=n.y_offset_px,
                        speed_mul=n.speed_mul,
                        size_px=n.size_px,
                        alpha01=n.alpha01,
                        hitsound_path=hs,
                        t_enter=n.t_enter,
                        mh=n.mh,
                    )
                    all_notes.append(nn)

                bgm_p = str(tr.get("bgm")) if tr.get("bgm") else (music_p if (music_p and os.path.exists(music_p)) else None)
                if bgm_p and (not os.path.isabs(bgm_p)):
                    cand = os.path.join(base_dir, bgm_p)
                    bgm_p = cand if os.path.exists(cand) else os.path.join(advance_base_dir, bgm_p)
                if bgm_p:
                    advance_tracks_bgm.append({"start_at": start_at, "end_at": end_at, "path": bgm_p, "idx": idx})
                if idx == main_idx:
                    advance_main_bgm = bgm_p

                if (not bg_path) and bg_p and os.path.exists(bg_p):
                    bg_path = bg_p
                    chart_info = info or {}
                    bd = chart_info.get("backgroundDim", None)
                    if bd is not None:
                        bg_dim_alpha = int(clamp(float(bd), 0.0, 1.0) * 255)

            fmt = "advance"
            offset = 0.0
            lines = all_lines
            notes = sorted(all_notes, key=lambda x: x.t_hit)

        line_map2 = {ln.lid: ln for ln in lines}
        for n in notes:
            ln = line_map2.get(n.line_id)
            if ln is None:
                continue
            n.scroll_hit = ln.scroll_px.integral(n.t_hit)
            n.scroll_end = ln.scroll_px.integral(n.t_end)

        chart_path = None

        return AdvanceLoadResult(
            fmt=fmt,
            offset=offset,
            lines=lines,
            notes=notes,
            advance_active=True,
            advance_cfg=advance_cfg,
            advance_mix=advance_mix,
            advance_tracks_bgm=advance_tracks_bgm,
            advance_main_bgm=advance_main_bgm,
            advance_segment_starts=advance_segment_starts,
            advance_segment_bgm=advance_segment_bgm,
            advance_base_dir=advance_base_dir,
            advance_mods=advance_mods,
            chart_path=chart_path,
            chart_info=chart_info,
            bg_path=bg_path,
            music_path=music_path,
            bg_dim_alpha=bg_dim_alpha,
            packs_keepalive=packs_keepalive,
        )

    if chart_path and (os.path.isdir(chart_path) or (os.path.isfile(chart_path) and str(chart_path).lower().endswith((".zip", ".pez")))):
        pack = load_chart_pack(chart_path)
        chart_path = pack.chart_path
        music_path = pack.music_path
        bg_path = pack.bg_path
        chart_info = pack.info

        bd = chart_info.get("backgroundDim", None)
        if bd is not None:
            bg_dim_alpha = int(clamp(float(bd), 0.0, 1.0) * 255)

        packs_keepalive.append(pack)

    fmt, offset, lines, notes = load_chart(chart_path, W, H)

    return AdvanceLoadResult(
        fmt=fmt,
        offset=offset,
        lines=lines,
        notes=notes,
        advance_active=False,
        advance_cfg=None,
        advance_mix=False,
        advance_tracks_bgm=[],
        advance_main_bgm=None,
        advance_segment_starts=[],
        advance_segment_bgm=[],
        advance_base_dir=None,
        advance_mods=None,
        chart_path=chart_path,
        chart_info=chart_info,
        bg_path=bg_path,
        music_path=music_path,
        bg_dim_alpha=bg_dim_alpha,
        packs_keepalive=packs_keepalive,
    )
