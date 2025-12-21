from __future__ import annotations

import argparse
import importlib.util
import os
import random
import sys
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional, Tuple

from ..io.chart_pack_impl import load_chart_pack
from ..math.util import clamp
from ..runtime.judge import Judge
from ..renderer import run as run_renderer
from ..config_v2 import flatten_config_v2, load_config_v2


@dataclass
class ChartMeta:
    input_path: str
    chart_path: str
    music_path: Optional[str]
    bg_path: Optional[str]
    chart_info: Dict[str, Any]
    bg_dim_alpha: Optional[int]
    total_notes: int
    seg_notes: int
    seg_end_time: float
    seg_duration: float
    seg_max_chord: int
    seg_note_hit_times: List[float]


@dataclass
class JumpDecision:
    action: str  # next | prev | jump | reshuffle | stop
    index: Optional[int] = None


ShouldJumpFn = Callable[[Dict[str, Any]], Optional[JumpDecision]]
PlaylistFilterFn = Callable[[ChartMeta], bool]
SortMetasFn = Callable[[List[ChartMeta], Any], List[ChartMeta]]


def _parse_csv(s: Optional[str]) -> List[str]:
    if not s:
        return []
    parts: List[str] = []
    for raw in str(s).replace(";", ",").split(","):
        t = str(raw).strip()
        if t:
            parts.append(t)
    return parts


def _match_meta_filters(
    meta: ChartMeta,
    *,
    levels: Optional[List[str]] = None,
    name_contains: Optional[str] = None,
    min_total_notes: Optional[int] = None,
    max_total_notes: Optional[int] = None,
) -> bool:
    if levels:
        lv = str((meta.chart_info or {}).get("level", "") or "").strip().upper()
        allow = {str(x).strip().upper() for x in (levels or []) if str(x).strip()}
        if allow and (lv not in allow):
            return False

    if name_contains:
        nm = str((meta.chart_info or {}).get("name", "") or "").lower()
        if str(name_contains).strip().lower() not in nm:
            return False

    if min_total_notes is not None:
        try:
            if int(meta.total_notes) < int(min_total_notes):
                return False
        except Exception:
            return False

    if max_total_notes is not None:
        try:
            if int(meta.total_notes) > int(max_total_notes):
                return False
        except Exception:
            return False

    return True


def discover_chart_inputs(charts_dir: str) -> List[str]:
    charts_dir = os.path.abspath(str(charts_dir))
    out: List[str] = []

    try:
        items = os.listdir(charts_dir)
    except Exception:
        return []

    for fn in items:
        p = os.path.join(charts_dir, fn)
        if os.path.isdir(p):
            if os.path.exists(os.path.join(p, "info.yml")):
                out.append(p)
            else:
                try:
                    sub = os.listdir(p)
                except Exception:
                    sub = []
                ok = False
                for sf in sub:
                    try:
                        low2 = sf.lower()
                        if low2.endswith(".json") and low2 not in {"info.json", "meta.json"}:
                            ok = True
                            break
                    except Exception:
                        continue
                if ok:
                    out.append(p)
            continue
        low = fn.lower()
        if low.endswith((".zip", ".pez")):
            out.append(p)
            continue
        if low.endswith((".json", ".pec", ".pe")):
            out.append(p)
            continue

    out.sort()
    return out


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


def _resolve_loose_chart_dir(dir_path: str, prefer: List[str]) -> Tuple[Optional[str], Optional[str], Optional[str], Optional[str]]:
    try:
        items = os.listdir(dir_path)
    except Exception:
        items = []

    jsons: List[str] = []
    for fn in items:
        try:
            low = fn.lower()
            if not low.endswith(".json"):
                continue
            if low in {"info.json", "meta.json"}:
                continue
            jsons.append(fn)
        except Exception:
            continue

    prefer_u = [str(x).strip().upper() for x in (prefer or []) if str(x).strip()]
    if not prefer_u:
        prefer_u = ["IN", "AT", "HD", "EZ"]

    chosen = None
    chosen_diff = None
    for d in prefer_u:
        cand = None
        for fn in jsons:
            stem, _ = os.path.splitext(fn)
            if stem.strip().upper() == d:
                cand = fn
                break
        if cand:
            chosen = cand
            chosen_diff = d
            break
    if chosen is None and jsons:
        jsons.sort()
        chosen = jsons[0]
        try:
            chosen_diff = os.path.splitext(os.path.basename(chosen))[0].strip().upper()
        except Exception:
            chosen_diff = None

    chart_p = os.path.join(dir_path, chosen) if chosen else None

    folder_name = os.path.basename(os.path.abspath(dir_path))
    bg_exts = (".png", ".jpg", ".jpeg", ".webp")
    bg_p = _pick_first_existing(
        dir_path,
        [
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
        ],
    )
    if not bg_p:
        bg_p = _auto_pick_asset_by_basename(dir_path, folder_name, bg_exts)

    audio_exts = (".ogg", ".mp3", ".wav")
    music_p = _pick_first_existing(
        dir_path,
        [
            "song.ogg",
            "song.mp3",
            "song.wav",
            "music.ogg",
            "music.mp3",
            "music.wav",
            "bgm.ogg",
            "bgm.mp3",
            "bgm.wav",
        ],
    )
    if not music_p:
        music_p = _auto_pick_asset_by_basename(dir_path, folder_name, audio_exts)

    return chart_p, music_p, bg_p, chosen_diff


def _bg_dim_alpha_from_info(info: Dict[str, Any]) -> Optional[int]:
    try:
        bd = info.get("backgroundDim", None)
        if bd is None:
            return None
        return int(clamp(float(bd), 0.0, 1.0) * 255)
    except Exception:
        return None


def _resolve_pack_or_chart(input_path: str) -> Tuple[str, Optional[str], Optional[str], Dict[str, Any]]:
    input_path = os.path.abspath(str(input_path))

    chart_path = input_path
    music_path = None
    bg_path = None
    chart_info: Dict[str, Any] = {}

    if os.path.isdir(input_path) and (not os.path.exists(os.path.join(input_path, "info.yml"))):
        prefer = ["IN", "AT", "HD", "EZ"]
        chart_p, music_p, bg_p, chosen_diff = _resolve_loose_chart_dir(input_path, prefer)
        if chart_p:
            chart_path = chart_p
        if music_p:
            music_path = music_p
        if bg_p:
            bg_path = bg_p

        # Minimal chart_info for UI overlay.
        # Pack charts will provide richer info via info.yml.
        folder_name = os.path.basename(os.path.abspath(input_path))
        chart_info = {
            "name": folder_name,
            "level": (str(chosen_diff) if chosen_diff else ""),
        }
    elif os.path.isdir(input_path) or (os.path.isfile(input_path) and input_path.lower().endswith((".zip", ".pez"))):
        p = load_chart_pack(input_path)
        chart_path = p.chart_path
        music_path = p.music_path
        bg_path = p.bg_path
        chart_info = p.info or {}

    return str(chart_path), (str(music_path) if music_path else None), (str(bg_path) if bg_path else None), chart_info


def _load_meta(input_path: str, W: int, H: int, *, notes_per_chart: int) -> Optional[ChartMeta]:
    from ..io.chart_loader_impl import load_chart

    try:
        chart_path, music_path, bg_path, chart_info = _resolve_pack_or_chart(input_path)
    except Exception:
        return None

    try:
        _fmt, _offset, _lines, notes = load_chart(chart_path, int(W), int(H))
    except Exception:
        return None

    playable = [n for n in notes if not getattr(n, "fake", False)]
    playable.sort(key=lambda x: float(getattr(x, "t_hit", 0.0)))

    total_notes = int(len(playable))

    seg_notes = max(0, min(int(notes_per_chart), len(playable)))

    seg_note_hit_times: List[float] = []
    if seg_notes > 0:
        try:
            for n in playable[:seg_notes]:
                seg_note_hit_times.append(float(getattr(n, "t_hit", 0.0) or 0.0))
        except Exception:
            seg_note_hit_times = []

    seg_max_chord = 1
    if seg_notes > 0:
        try:
            eps = 1e-4
            cur = 0
            last_t = None
            mx = 1
            for n in playable[:seg_notes]:
                t0 = float(getattr(n, "t_hit", 0.0) or 0.0)
                if last_t is None or abs(float(t0) - float(last_t)) <= float(eps):
                    cur += 1
                else:
                    mx = max(int(mx), int(cur))
                    cur = 1
                last_t = t0
            mx = max(int(mx), int(cur))
            seg_max_chord = int(mx)
        except Exception:
            seg_max_chord = 1
    tail = 0.0
    for n in playable[:seg_notes]:
        try:
            tail = max(float(tail), float(getattr(n, "t_end", getattr(n, "t_hit", 0.0))))
        except Exception:
            pass
    seg_end_time = float(tail) if seg_notes > 0 else 0.0

    return ChartMeta(
        input_path=os.path.abspath(str(input_path)),
        chart_path=str(chart_path),
        music_path=music_path,
        bg_path=bg_path,
        chart_info=dict(chart_info or {}),
        bg_dim_alpha=_bg_dim_alpha_from_info(chart_info),
        total_notes=int(total_notes),
        seg_notes=int(seg_notes),
        seg_end_time=float(seg_end_time),
        seg_duration=float(seg_end_time),
        seg_max_chord=int(seg_max_chord),
        seg_note_hit_times=list(seg_note_hit_times),
    )


def build_chart_metas(
    *,
    charts_dir: str,
    W: int,
    H: int,
    notes_per_chart: int = 10,
    seed: Optional[int] = None,
    shuffle: bool = True,
    filter_levels: Optional[List[str]] = None,
    filter_name_contains: Optional[str] = None,
    filter_min_total_notes: Optional[int] = None,
    filter_max_total_notes: Optional[int] = None,
    filter_limit: Optional[int] = None,
    filter_fn: Optional[PlaylistFilterFn] = None,
) -> List[ChartMeta]:
    inputs = discover_chart_inputs(str(charts_dir))
    if not inputs:
        return []

    if shuffle:
        rnd = random.Random(seed)
        rnd.shuffle(inputs)

    metas: List[ChartMeta] = []
    for p in inputs:
        m = _load_meta(p, W, H, notes_per_chart=int(notes_per_chart))
        if m is None:
            continue
        if int(m.seg_notes) <= 0:
            continue
        if not _match_meta_filters(
            m,
            levels=filter_levels,
            name_contains=filter_name_contains,
            min_total_notes=filter_min_total_notes,
            max_total_notes=filter_max_total_notes,
        ):
            continue
        if filter_fn is not None:
            try:
                if not bool(filter_fn(m)):
                    continue
            except Exception:
                continue
        metas.append(m)

        if filter_limit is not None and int(filter_limit) > 0 and len(metas) >= int(filter_limit):
            break

    return metas


def run_playlist(
    args: Any,
    *,
    metas: List[ChartMeta],
    switch_mode: str = "hit",
    seed: Optional[int] = None,
    start_index: int = 0,
    initial_time_offset: float = 0.0,
    first_seg_start_time: float = 0.0,
    first_seg_skip_notes: int = 0,
    initial_combo_total: int = 0,
) -> Judge:
    if not metas:
        raise SystemExit("No playable charts found")

    total_notes = int(sum(int(m.seg_notes) for m in metas))
    total_duration = float(sum(float(m.seg_duration) for m in metas))

    W = int(getattr(args, "w", 1280))
    H = int(getattr(args, "h", 720))
    expand = float(getattr(args, "expand", 1.0) or 1.0)

    judge = Judge()
    if int(initial_combo_total) > 0:
        try:
            v = int(initial_combo_total)
            judge.combo = int(v)
            judge.max_combo = int(v)
            judge.hit_total = int(v)
            judge.judged_cnt = int(v)
            judge.acc_sum = float(v)
        except Exception:
            pass

    orig_start_time = getattr(args, "start_time", None)
    orig_end_time = getattr(args, "end_time", None)
    orig_bg = getattr(args, "bg", None)
    orig_bgm = getattr(args, "bgm", None)

    reuse_pygame = bool(str(getattr(args, "backend", "pygame") or "pygame").strip().lower() == "pygame")
    reuse_audio = False
    shared_audio = None
    if reuse_pygame:
        try:
            from ..audio import create_audio_backend

            shared_audio = create_audio_backend(getattr(args, "audio_backend", "pygame"))
            reuse_audio = True
        except Exception:
            shared_audio = None
            reuse_audio = False

    idx = max(0, int(start_index))
    time_offset = float(initial_time_offset)

    mode = str(switch_mode).strip().lower()
    user_should_jump: Optional[ShouldJumpFn] = None
    if hasattr(args, "playlist_should_jump") and callable(getattr(args, "playlist_should_jump")):
        user_should_jump = getattr(args, "playlist_should_jump")

    jump_after_time = getattr(args, "jump_after_time", None)
    jump_after_time = float(jump_after_time) if jump_after_time is not None else None

    stop_after_total_hits = getattr(args, "stop_after_total_hits", None)
    stop_after_total_hits = int(stop_after_total_hits) if stop_after_total_hits is not None else None

    stop_after_total_seconds = getattr(args, "stop_after_total_seconds", None)
    stop_after_total_seconds = float(stop_after_total_seconds) if stop_after_total_seconds is not None else None

    jump_random_prob = getattr(args, "jump_random_prob", None)
    jump_random_prob = float(jump_random_prob) if jump_random_prob is not None else None
    jump_random_prob = float(jump_random_prob) if jump_random_prob is not None else 0.0
    if jump_random_prob < 0.0:
        jump_random_prob = 0.0
    if jump_random_prob > 1.0:
        jump_random_prob = 1.0

    rnd = random.Random(seed)

    def _builtin_should_jump(ctx: Dict[str, Any]) -> Optional[JumpDecision]:
        try:
            t_now = float(ctx.get("t", 0.0) or 0.0)
            playlist_time = float(ctx.get("playlist_time", 0.0) or 0.0)
        except Exception:
            t_now = 0.0
            playlist_time = 0.0

        j = ctx.get("judge")
        if stop_after_total_hits is not None:
            try:
                if int(getattr(j, "hit_total", 0)) >= int(stop_after_total_hits):
                    return JumpDecision(action="stop")
            except Exception:
                pass

        if stop_after_total_seconds is not None:
            try:
                if float(playlist_time) >= float(stop_after_total_seconds):
                    return JumpDecision(action="stop")
            except Exception:
                pass

        if jump_after_time is not None:
            try:
                if float(t_now) >= float(jump_after_time):
                    return JumpDecision(action="next")
            except Exception:
                pass

        return None

    def _should_jump(ctx: Dict[str, Any]) -> Optional[JumpDecision]:
        if user_should_jump is not None:
            try:
                dec = user_should_jump(ctx)
                if dec is not None and str(getattr(dec, "action", "")):
                    return dec
            except Exception:
                pass
        return _builtin_should_jump(ctx)

    try:
        while 0 <= idx < len(metas):
            meta = metas[int(idx)]

            seg_skip = 0
            seg_start_time = 0.0
            if int(idx) == int(start_index):
                seg_skip = max(0, int(first_seg_skip_notes))
                seg_start_time = max(0.0, float(first_seg_start_time))
                if seg_skip > int(meta.seg_notes):
                    seg_skip = int(meta.seg_notes)

            pending_dec: Optional[JumpDecision] = None

            stop_hit_total = None
            stop_judged_cnt = None
            seg_remain = max(0, int(meta.seg_notes) - int(seg_skip))
            if mode == "hit":
                stop_hit_total = int(getattr(judge, "hit_total", 0)) + int(seg_remain)
            elif mode == "judged":
                stop_judged_cnt = int(getattr(judge, "judged_cnt", 0)) + int(seg_remain)
            else:
                stop_hit_total = int(getattr(judge, "hit_total", 0)) + int(seg_remain)

            fmt, offset, lines, notes = _load_for_play(meta, W, H)

            def _mk_ctx(extra: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
                base: Dict[str, Any] = {
                    "playlist_index": int(idx),
                    "playlist_size": int(len(metas)),
                    "playlist_total_notes": int(total_notes),
                    "playlist_total_duration": float(total_duration),
                    "playlist_time_offset": float(time_offset),
                    "playlist_time": float(time_offset) + float(extra.get("t", 0.0) if extra else 0.0),
                    "meta": meta,
                    "judge": judge,
                    "combo": int(getattr(judge, "combo", 0) or 0),
                    "max_combo": int(getattr(judge, "max_combo", 0) or 0),
                    "judged_cnt": int(getattr(judge, "judged_cnt", 0) or 0),
                    "hit_total": int(getattr(judge, "hit_total", 0) or 0),
                    "acc_sum": float(getattr(judge, "acc_sum", 0.0) or 0.0),
                }
                try:
                    base["acc_ratio"] = float(base["acc_sum"]) / float(total_notes) if int(total_notes) > 0 else 0.0
                except Exception:
                    base["acc_ratio"] = 0.0
                if extra:
                    try:
                        for k, v in extra.items():
                            if k not in base:
                                base[k] = v
                    except Exception:
                        pass
                return base

            def _stop_cb(ctx: Dict[str, Any]) -> bool:
                nonlocal pending_dec
                try:
                    merged = dict(ctx or {})
                    merged["t"] = float(ctx.get("t", 0.0))
                    merged["judge"] = ctx.get("judge")
                    dec = _should_jump(_mk_ctx(merged))
                    if dec is not None and str(getattr(dec, "action", "")):
                        pending_dec = dec
                        return True
                except Exception:
                    pass
                return False

            try:
                setattr(args, "start_time", float(seg_start_time) if seg_start_time > 1e-9 else 0.0)
                setattr(args, "end_time", float(meta.seg_end_time))

                # Playlist segments should use per-chart assets by default.
                # If CLI/config provided a global bg/bgm, it would otherwise override switching.
                try:
                    setattr(args, "bg", None)
                    setattr(args, "bgm", None)
                except Exception:
                    pass

                extra_ctx: Dict[str, Any] = {}
                if reuse_pygame:
                    extra_ctx["reuse_pygame"] = True
                    extra_ctx["reuse_audio"] = bool(reuse_audio)
                    extra_ctx["audio"] = shared_audio

                run_renderer(
                    args,
                    W=int(W),
                    H=int(H),
                    expand=float(expand),
                    fmt=str(fmt),
                    offset=float(offset),
                    lines=lines,
                    notes=notes,
                    chart_info=dict(meta.chart_info or {}),
                    bg_dim_alpha=meta.bg_dim_alpha,
                    bg_path=meta.bg_path,
                    music_path=meta.music_path,
                    chart_path=str(meta.chart_path),
                    advance_active=False,
                    advance_cfg=None,
                    advance_mix=False,
                    advance_tracks_bgm=[],
                    advance_main_bgm=None,
                    advance_segment_starts=[],
                    advance_segment_bgm=[],
                    advance_base_dir=os.path.dirname(os.path.abspath(str(meta.chart_path))),
                    judge=judge,
                    total_notes_override=int(total_notes),
                    chart_end_override=float(total_duration),
                    chart_info_override=dict(meta.chart_info or {}),
                    ui_time_offset=float(time_offset),
                    stop_when_judged_cnt=stop_judged_cnt,
                    stop_when_hit_total=stop_hit_total,
                    should_stop_cb=_stop_cb,
                    **extra_ctx,
                )
            finally:
                try:
                    setattr(args, "start_time", orig_start_time)
                    setattr(args, "end_time", orig_end_time)
                    setattr(args, "bg", orig_bg)
                    setattr(args, "bgm", orig_bgm)
                except Exception:
                    pass

            if int(idx) == int(start_index) and seg_start_time > 1e-9:
                time_offset += max(0.0, float(meta.seg_duration) - float(seg_start_time))
            else:
                time_offset += float(meta.seg_duration)

            dec = pending_dec
            if dec is None:
                try:
                    dec = _should_jump(_mk_ctx({"t": float(meta.seg_end_time)}))
                except Exception:
                    dec = None

            if dec is None:
                idx += 1
                continue

            act = str(getattr(dec, "action", "next")).strip().lower()
            if act == "stop":
                break
            if act == "prev":
                idx = max(0, int(idx) - 1)
                continue
            if act == "jump":
                if dec.index is None:
                    idx += 1
                else:
                    idx = int(dec.index)
                continue
            if act == "reshuffle":
                rnd = random.Random(seed)
                rnd.shuffle(metas)
                idx = 0
                time_offset = 0.0
                continue

            if jump_random_prob > 1e-9:
                try:
                    if rnd.random() < float(jump_random_prob):
                        j = rnd.randrange(0, int(len(metas)))
                        if int(len(metas)) > 1 and int(j) == int(idx):
                            j = (int(j) + 1) % int(len(metas))
                        idx = int(j)
                        continue
                except Exception:
                    pass

            idx += 1
    finally:
        if shared_audio is not None and bool(reuse_audio):
            try:
                shared_audio.close()
            except Exception:
                pass

    return judge


def _load_for_play(meta: ChartMeta, W: int, H: int):
    from ..io.chart_loader_impl import load_chart

    fmt, offset, lines, notes = load_chart(str(meta.chart_path), int(W), int(H))
    return fmt, offset, lines, notes


def default_should_jump(ctx: Dict[str, Any]) -> Optional[JumpDecision]:
    # Default: do not jump (playlist advances sequentially).
    return None


def run_random_playlist(
    args: Any,
    *,
    charts_dir: str,
    notes_per_chart: int = 10,
    seed: Optional[int] = None,
    shuffle: bool = True,
    switch_mode: str = "hit",
    filter_levels: Optional[List[str]] = None,
    filter_name_contains: Optional[str] = None,
    filter_min_total_notes: Optional[int] = None,
    filter_max_total_notes: Optional[int] = None,
    filter_limit: Optional[int] = None,
    filter_fn: Optional[PlaylistFilterFn] = None,
) -> Judge:
    W = int(getattr(args, "w", 1280))
    H = int(getattr(args, "h", 720))
    metas = build_chart_metas(
        charts_dir=str(charts_dir),
        W=int(W),
        H=int(H),
        notes_per_chart=int(notes_per_chart),
        seed=seed,
        shuffle=bool(shuffle),
        filter_levels=filter_levels,
        filter_name_contains=filter_name_contains,
        filter_min_total_notes=filter_min_total_notes,
        filter_max_total_notes=filter_max_total_notes,
        filter_limit=filter_limit,
        filter_fn=filter_fn,
    )
    if not metas:
        raise SystemExit(f"No charts found in: {charts_dir}")
    return run_playlist(args, metas=metas, switch_mode=str(switch_mode), seed=seed)


def apply_config_v2_to_args(args: Any, *, argv: Optional[List[str]] = None) -> None:
    cfg_path = getattr(args, "config", None)
    if not cfg_path:
        return

    try:
        cfg_v2_raw = load_config_v2(str(cfg_path))
        flat_cfg, _mods_cfg = flatten_config_v2(cfg_v2_raw)
    except Exception:
        return

    argv_l = list(argv) if argv is not None else list(getattr(sys, "argv", []) or [])

    for k, v in (flat_cfg or {}).items():
        try:
            if not hasattr(args, k):
                continue
            if ("--" + str(k)) in argv_l:
                continue
            setattr(args, k, v)
        except Exception:
            continue


def setup_recorder_from_args(args: Any, *, W: int, H: int) -> Any:
    mode = str(getattr(args, "record_mode", "off") or "off").strip().lower()
    if mode in {"0", "false", "none"}:
        mode = "off"

    if mode in {"off", ""}:
        return None

    out = getattr(args, "record_out", None)
    if not out:
        raise SystemExit("--record_out is required when --record_mode is enabled")

    fps = float(getattr(args, "record_fps", 60.0) or 60.0)
    if fps <= 1e-6:
        fps = 60.0

    recorder = None
    if mode == "frames":
        from ..recording.frame_recorder import FrameRecorder

        os.makedirs(str(out), exist_ok=True)
        recorder = FrameRecorder(str(out), int(W), int(H), float(fps))

    elif mode == "video":
        from ..recording.video_recorder import VideoRecorder

        preset = str(getattr(args, "record_preset", "balanced") or "balanced")
        codec = str(getattr(args, "record_codec", "libx264") or "libx264")
        recorder = VideoRecorder(
            output_file=str(out),
            width=int(W),
            height=int(H),
            fps=float(fps),
            preset=preset,
            audio_path=None,
            codec=codec,
        )
    else:
        raise SystemExit(f"Unknown record_mode: {mode}")

    # Attach to args in the same convention as phic_renderer.record
    setattr(args, "recorder", recorder)
    setattr(args, "record_enabled", True)
    setattr(args, "record_fps", float(fps))
    setattr(args, "record_start_time", float(getattr(args, "record_start_time", 0.0) or 0.0))
    setattr(args, "record_end_time", getattr(args, "record_end_time", None))
    setattr(args, "record_headless", bool(getattr(args, "record_headless", True)))
    setattr(args, "record_log_interval", float(getattr(args, "record_log_interval", 1.0) or 1.0))
    setattr(args, "record_log_notes", bool(getattr(args, "record_log_notes", False)))
    setattr(args, "record_use_curses", bool(getattr(args, "record_use_curses", False)) and bool(getattr(args, "record_headless", True)))
    setattr(args, "record_render_particles", (not bool(getattr(args, "no_particles", False))))
    setattr(args, "record_render_text", (not bool(getattr(args, "no_text", False))))

    return recorder


def load_playlist_script(script_path: str):
    p = os.path.abspath(str(script_path))
    if not os.path.exists(p):
        raise SystemExit(f"playlist_script not found: {p}")
    mod_name = "phic_playlist_script"
    spec = importlib.util.spec_from_file_location(mod_name, p)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Failed to load playlist_script: {p}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_playlist_script(args: Any) -> Judge:
    script_path = getattr(args, "playlist_script", None)
    if not script_path:
        raise SystemExit("playlist_script is required")

    mod = load_playlist_script(str(script_path))

    if hasattr(mod, "configure_args") and callable(getattr(mod, "configure_args")):
        try:
            getattr(mod, "configure_args")(args)
        except Exception:
            pass

    if hasattr(mod, "playlist_should_jump") and callable(getattr(mod, "playlist_should_jump")):
        try:
            setattr(args, "playlist_should_jump", getattr(mod, "playlist_should_jump"))
        except Exception:
            pass

    if hasattr(mod, "playlist_filter") and callable(getattr(mod, "playlist_filter")):
        try:
            setattr(args, "playlist_filter", getattr(mod, "playlist_filter"))
        except Exception:
            pass

    charts_dir = str(getattr(args, "playlist_charts_dir", getattr(args, "charts_dir", "charts")) or "charts")
    notes_per_chart = int(getattr(args, "playlist_notes_per_chart", 10) or 10)
    seed = getattr(args, "playlist_seed", None)
    seed = int(seed) if seed is not None else None
    shuffle = not bool(getattr(args, "playlist_no_shuffle", False))
    switch_mode = str(getattr(args, "playlist_switch_mode", "hit") or "hit")

    filter_levels = _parse_csv(getattr(args, "playlist_filter_levels", None))
    filter_name_contains = getattr(args, "playlist_filter_name_contains", None)
    filter_min_total_notes = getattr(args, "playlist_filter_min_total_notes", None)
    filter_max_total_notes = getattr(args, "playlist_filter_max_total_notes", None)
    filter_limit = getattr(args, "playlist_filter_limit", None)

    filter_fn = None
    if hasattr(args, "playlist_filter") and callable(getattr(args, "playlist_filter")):
        filter_fn = getattr(args, "playlist_filter")

    W = int(getattr(args, "w", 1280) or 1280)
    H = int(getattr(args, "h", 720) or 720)

    metas: List[ChartMeta]
    if hasattr(mod, "build_metas") and callable(getattr(mod, "build_metas")):
        metas = list(getattr(mod, "build_metas")(args) or [])
    else:
        metas = build_chart_metas(
            charts_dir=str(charts_dir),
            W=int(W),
            H=int(H),
            notes_per_chart=int(notes_per_chart),
            seed=seed,
            shuffle=bool(shuffle),
            filter_levels=filter_levels,
            filter_name_contains=filter_name_contains,
            filter_min_total_notes=filter_min_total_notes,
            filter_max_total_notes=filter_max_total_notes,
            filter_limit=filter_limit,
            filter_fn=filter_fn,
        )

    if hasattr(mod, "sort_metas") and callable(getattr(mod, "sort_metas")):
        try:
            metas = list(getattr(mod, "sort_metas")(metas, args) or metas)
        except Exception:
            pass

    # Start position:
    # - fresh: start from index but treat as new playlist (slice metas)
    # - resume: start from index within full playlist and keep ui_time_offset
    start_mode = str(getattr(args, "playlist_start_mode", "fresh") or "fresh").strip().lower()
    start_idx = getattr(args, "playlist_start_index", None)
    start_idx = int(start_idx) if start_idx is not None else 0

    start_from_combo_total = getattr(args, "playlist_start_from_combo_total", None)
    if start_from_combo_total is None:
        start_from_combo_total = getattr(args, "playlist_start_from_hit_total", None)

    first_seg_skip = 0
    first_seg_start_time = 0.0
    initial_combo_total = 0

    if start_from_combo_total is not None:
        try:
            tgt = max(0, int(start_from_combo_total))
            pref = 0
            found = 0
            for i, m in enumerate(list(metas)):
                sn = int(getattr(m, "seg_notes", 0) or 0)
                nxt = pref + sn
                if tgt < nxt:
                    found = int(i)
                    first_seg_skip = max(0, int(tgt - pref))
                    break
                pref = nxt
                found = int(i) + 1
            start_idx = int(found)

            initial_combo_total = int(tgt)
            if 0 <= int(start_idx) < len(metas):
                mm = metas[int(start_idx)]
                ts = list(getattr(mm, "seg_note_hit_times", []) or [])
                if 0 <= int(first_seg_skip) < len(ts):
                    first_seg_start_time = float(ts[int(first_seg_skip)])
                else:
                    first_seg_start_time = 0.0
        except Exception:
            pass

    if hasattr(mod, "select_start_index") and callable(getattr(mod, "select_start_index")):
        try:
            start_idx = int(getattr(mod, "select_start_index")(metas, args))
        except Exception:
            pass

    if start_idx < 0:
        start_idx = 0
    if start_idx >= len(metas):
        start_idx = max(0, len(metas) - 1)

    if start_mode == "fresh":
        metas2 = list(metas[start_idx:])
        return run_playlist(
            args,
            metas=metas2,
            switch_mode=str(switch_mode),
            seed=seed,
            start_index=0,
            initial_time_offset=0.0,
            first_seg_start_time=float(first_seg_start_time),
            first_seg_skip_notes=int(first_seg_skip),
            initial_combo_total=int(initial_combo_total),
        )

    # resume
    try:
        init_off = float(sum(float(getattr(m, "seg_duration", 0.0) or 0.0) for m in metas[:start_idx]))
    except Exception:
        init_off = 0.0
    return run_playlist(
        args,
        metas=metas,
        switch_mode=str(switch_mode),
        seed=seed,
        start_index=int(start_idx),
        initial_time_offset=float(init_off),
        first_seg_start_time=float(first_seg_start_time),
        first_seg_skip_notes=int(first_seg_skip),
        initial_combo_total=int(initial_combo_total),
    )


def _build_argparser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="phic_renderer.api.playlist")
    ap.add_argument("--config", type=str, default=None, help="Config v2 (JSONC) path")
    ap.add_argument("--charts", required=True, help="Charts directory")
    ap.add_argument("--notes_per_chart", type=int, default=10)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--no_shuffle", action="store_true")
    ap.add_argument("--switch_mode", type=str, default="hit", choices=["hit", "judged"])

    ap.add_argument("--filter_levels", type=str, default=None, help="CSV levels, e.g. IN,HD,EZ")
    ap.add_argument("--filter_name_contains", type=str, default=None)
    ap.add_argument("--filter_min_total_notes", type=int, default=None)
    ap.add_argument("--filter_max_total_notes", type=int, default=None)
    ap.add_argument("--filter_limit", type=int, default=None)

    ap.add_argument("--w", type=int, default=1280)
    ap.add_argument("--h", type=int, default=720)
    ap.add_argument("--expand", type=float, default=1.0)
    ap.add_argument("--backend", type=str, default="pygame")

    ap.add_argument("--respack", type=str, default=None)
    ap.add_argument("--bg", type=str, default=None)
    ap.add_argument("--bg_blur", type=int, default=10)
    ap.add_argument("--bg_dim", type=int, default=120)

    ap.add_argument("--bgm_volume", type=float, default=0.8)
    ap.add_argument("--audio_backend", type=str, default="pygame")

    ap.add_argument("--judge_width", type=float, default=0.12)
    ap.add_argument("--flick_threshold", type=float, default=0.02)

    ap.add_argument("--no_title_overlay", action="store_true")
    ap.add_argument("--font_path", type=str, default=None)
    ap.add_argument("--font_size_multiplier", type=float, default=1.0)

    ap.add_argument("--hit_debug", action="store_true")
    ap.add_argument("--debug_particles", action="store_true")

    ap.add_argument("--record_mode", type=str, default="off", choices=["off", "frames", "video"])
    ap.add_argument("--record_out", type=str, default=None, help="frames: output dir; video: output file")
    ap.add_argument("--record_fps", type=float, default=60.0)
    ap.add_argument("--record_headless", action="store_true")
    ap.add_argument("--record_start_time", type=float, default=0.0)
    ap.add_argument("--record_end_time", type=float, default=None)
    ap.add_argument("--record_log_interval", type=float, default=1.0)
    ap.add_argument("--record_log_notes", action="store_true")
    ap.add_argument("--record_use_curses", action="store_true")
    ap.add_argument("--record_preset", type=str, default="balanced")
    ap.add_argument("--record_codec", type=str, default="libx264")

    ap.add_argument("--jump_after_time", type=float, default=None)
    ap.add_argument("--stop_after_total_hits", type=int, default=None)
    ap.add_argument("--stop_after_total_seconds", type=float, default=None)
    ap.add_argument("--jump_random_prob", type=float, default=None)

    return ap


def main():
    ap = _build_argparser()
    args = ap.parse_args()

    apply_config_v2_to_args(args, argv=list(sys.argv))

    recorder = None
    try:
        recorder = setup_recorder_from_args(args, W=int(getattr(args, "w", 1280)), H=int(getattr(args, "h", 720)))
        if recorder is not None:
            try:
                recorder.open()
            except Exception as e:
                raise SystemExit(f"Failed to initialize recorder: {e}")

        run_random_playlist(
            args,
            charts_dir=str(args.charts),
            notes_per_chart=int(args.notes_per_chart),
            seed=args.seed,
            shuffle=(not bool(args.no_shuffle)),
            switch_mode=str(args.switch_mode),
            filter_levels=_parse_csv(getattr(args, "filter_levels", None)),
            filter_name_contains=getattr(args, "filter_name_contains", None),
            filter_min_total_notes=getattr(args, "filter_min_total_notes", None),
            filter_max_total_notes=getattr(args, "filter_max_total_notes", None),
            filter_limit=getattr(args, "filter_limit", None),
        )
    finally:
        if recorder is not None:
            try:
                recorder.close()
            except Exception:
                pass


if __name__ == "__main__":
    main()
