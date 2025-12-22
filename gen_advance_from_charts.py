from __future__ import annotations

import argparse
import json
import os
import random
import sys
from typing import Any, Dict, List, Optional, Tuple

from phic_renderer.io.chart_loader_impl import load_chart


def _parse_levels_csv(s: Optional[str]) -> Optional[List[str]]:
    if not s:
        return None
    try:
        out = [x.strip().upper() for x in str(s).split(",") if str(x).strip()]
    except Exception:
        out = []
    return out or None


def _list_chart_inputs(charts_dir: str, *, levels: Optional[List[str]]) -> List[str]:
    """Return a list of inputs accepted by advance loader: json files OR pack .zip/.pez files."""
    charts_dir = os.path.abspath(str(charts_dir))
    out: List[str] = []
    try:
        items = os.listdir(charts_dir)
    except Exception:
        return []

    for fn in items:
        p = os.path.join(charts_dir, fn)
        low = fn.lower()

        # Pack files
        if os.path.isfile(p) and low.endswith((".zip", ".pez")):
            out.append(p)
            continue

        # Loose single json at root
        if os.path.isfile(p) and low.endswith(".json") and low not in {"info.json", "meta.json"}:
            if levels is not None:
                try:
                    stem = os.path.splitext(os.path.basename(p))[0].strip().upper()
                    if stem not in set(levels):
                        continue
                except Exception:
                    pass
            out.append(p)
            continue

        # Loose folder: charts/<song>/(IN.json/HD.json/.. + song.ogg + song.png)
        if os.path.isdir(p):
            try:
                sub = os.listdir(p)
            except Exception:
                sub = []

            jsons: List[str] = []
            for sf in sub:
                low2 = sf.lower()
                if not low2.endswith(".json"):
                    continue
                if low2 in {"info.json", "meta.json"}:
                    continue
                if levels is not None:
                    try:
                        stem = os.path.splitext(sf)[0].strip().upper()
                        if stem not in set(levels):
                            continue
                    except Exception:
                        pass
                jsons.append(sf)

            if not jsons:
                continue

            # stable order by prefer levels if provided
            if levels is not None:
                pref = {lv: i for i, lv in enumerate(list(levels))}

                def _key(x: str) -> Tuple[int, str]:
                    stem = os.path.splitext(x)[0].strip().upper()
                    return (int(pref.get(stem, 9999)), str(x).lower())

                jsons.sort(key=_key)
            else:
                jsons.sort(key=lambda x: str(x).lower())

            for sf in jsons:
                out.append(os.path.join(p, sf))

    out.sort(key=lambda x: str(x).lower())
    return out


def _pick_assets_for_chart(chart_path: str) -> Tuple[Optional[str], Optional[str]]:
    """Best-effort asset inference for loose folders.

    Typical layout:
      charts/<folder>/<diff>.json
      charts/<folder>/<folder>.ogg
      charts/<folder>/<folder>.png
    """
    base_dir = os.path.dirname(os.path.abspath(str(chart_path)))
    folder = os.path.basename(os.path.abspath(base_dir))

    def _first_existing(cands: List[str]) -> Optional[str]:
        for fn in cands:
            p = os.path.join(base_dir, fn)
            if os.path.exists(p):
                return p
        return None

    # Prefer common names
    bg = _first_existing(
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
        ]
    )

    # Prefer <folder>.*
    if bg is None:
        for ext in (".png", ".jpg", ".jpeg", ".webp"):
            p = os.path.join(base_dir, f"{folder}{ext}")
            if os.path.exists(p):
                bg = p
                break

    bgm = _first_existing(["song.ogg", "song.mp3", "song.wav"])
    if bgm is None:
        for ext in (".ogg", ".mp3", ".wav"):
            p = os.path.join(base_dir, f"{folder}{ext}")
            if os.path.exists(p):
                bgm = p
                break

    # Fallback: single audio/image in folder
    if bgm is None or bg is None:
        try:
            items = os.listdir(base_dir)
        except Exception:
            items = []
        if bgm is None:
            aud = [x for x in items if str(x).lower().endswith((".ogg", ".mp3", ".wav"))]
            if len(aud) == 1:
                bgm = os.path.join(base_dir, aud[0])
        if bg is None:
            imgs = [x for x in items if str(x).lower().endswith((".png", ".jpg", ".jpeg", ".webp"))]
            if len(imgs) == 1:
                bg = os.path.join(base_dir, imgs[0])

    return bg, bgm


def _seg_end_time_for_first_n_notes(notes: List[Any], n_notes: int) -> float:
    playable = [n for n in notes if not getattr(n, "fake", False)]
    playable.sort(key=lambda x: float(getattr(x, "t_hit", 0.0) or 0.0))
    if n_notes <= 0:
        return 0.0
    playable = playable[: min(int(n_notes), len(playable))]
    tail = 0.0
    for n in playable:
        try:
            tail = max(tail, float(getattr(n, "t_end", getattr(n, "t_hit", 0.0)) or 0.0))
        except Exception:
            pass
    return float(tail)


def build_advance_sequence(
    *,
    chart_paths: List[str],
    W: int,
    H: int,
    notes_per_chart: int,
    tail_time: float,
    bgm_override: Optional[str],
    chart_speed: float,
    include_bg: bool,
    include_bgm: bool,
    quiet: bool,
) -> Dict[str, Any]:
    items: List[Dict[str, Any]] = []

    def _log(msg: str) -> None:
        if quiet:
            return
        try:
            sys.stderr.write(str(msg).rstrip() + "\n")
        except Exception:
            pass

    t0 = 0.0
    n_total = int(len(chart_paths))
    for i, cp in enumerate(chart_paths, start=1):
        cp_s = str(cp)
        is_pack = cp_s.lower().endswith((".zip", ".pez"))
        kind = "pack" if is_pack else "loose"
        _log(f"[{i:4d}/{n_total:4d}] {kind}: {cp_s}")

        try:
            fmt, offset, lines, notes = load_chart(cp_s, int(W), int(H))
        except Exception as e:
            _log(f"  !! load_chart failed: {type(e).__name__}: {e}")
            continue

        seg_end = _seg_end_time_for_first_n_notes(notes, int(notes_per_chart))
        seg_dur = float(seg_end) + max(0.0, float(tail_time))
        _log(f"  seg_end={float(seg_end):.3f}s  tail={float(tail_time):.3f}s  seg_dur={float(seg_dur):.3f}s  start_at={float(t0):.3f}s")

        bg, bgm = _pick_assets_for_chart(cp)
        if bgm_override:
            bgm = str(bgm_override)

        it: Dict[str, Any] = {
            "input": str(cp),
            "start": 0.0,
            "end": float(seg_dur),
            "start_at": float(t0),
            "time_offset": 0.0,
            "chart_speed": float(chart_speed),
        }

        if bool(include_bgm) and bgm:
            it["bgm"] = str(bgm)
        if bool(include_bg) and bg:
            it["bg"] = str(bg)

        items.append(it)
        t0 += float(seg_dur)

    _log(f"Total segments: {len(items)}")
    _log(f"Total duration (sum seg_dur): {float(t0):.3f}s")

    return {"mode": "sequence", "mix": False, "items": items}


def _write_json(path: str, obj: Dict[str, Any]) -> None:
    with open(str(path), "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)


def main() -> None:
    ap = argparse.ArgumentParser(prog="gen_advance_from_charts.py")
    ap.add_argument("--charts_dir", type=str, default="charts")
    ap.add_argument("--output", type=str, default="advance.json")
    ap.add_argument("--w", type=int, default=1280)
    ap.add_argument("--h", type=int, default=720)
    ap.add_argument("--notes_per_chart", type=int, default=10)
    ap.add_argument("--tail_time", type=float, default=0.5)
    ap.add_argument("--bgm", type=str, default=None)
    ap.add_argument("--chart_speed", type=float, default=1.0)
    ap.add_argument(
        "--levels",
        type=str,
        default="AT,IN,HD,EZ",
        help="CSV difficulty filter+order for loose folders, e.g. AT,IN,HD,EZ",
    )
    ap.add_argument("--include_bg", action="store_true", help="(compat) Write bg field for first segment")
    ap.add_argument("--include_bgm", action="store_true", help="(compat) Write bgm field for each segment")
    ap.add_argument("--no_bg", action="store_true", help="Do not write bg field")
    ap.add_argument("--no_bgm", action="store_true", help="Do not write bgm field")
    ap.add_argument(
        "--order",
        type=str,
        default="fixed",
        choices=["fixed", "random"],
        help="Input ordering: fixed (sorted) or random (shuffled)",
    )
    ap.add_argument("--seed", type=int, default=None, help="Random seed for --order random")
    ap.add_argument(
        "--output_random",
        type=str,
        default=None,
        help="If set, also write a shuffled version to this path (while --output stays fixed order)",
    )
    ap.add_argument("--quiet", action="store_true", help="Disable progress output")
    args = ap.parse_args()

    def _log(msg: str) -> None:
        if bool(getattr(args, "quiet", False)):
            return
        try:
            sys.stderr.write(str(msg).rstrip() + "\n")
        except Exception:
            pass

    levels = _parse_levels_csv(getattr(args, "levels", None))
    chart_paths = _list_chart_inputs(str(args.charts_dir), levels=levels)
    if not chart_paths:
        raise SystemExit(f"No chart json found in: {args.charts_dir}")

    _log(f"charts_dir: {os.path.abspath(str(args.charts_dir))}")
    _log(f"found inputs: {len(chart_paths)}")
    if levels is not None:
        _log(f"levels: {','.join(levels)}")
    _log(f"notes_per_chart: {int(args.notes_per_chart)}")
    _log(f"tail_time: {float(args.tail_time):.3f}s")
    _log(f"chart_speed: {float(args.chart_speed):.3f}")

    chart_paths_fixed = list(chart_paths)
    chart_paths_random = list(chart_paths)
    try:
        rnd = random.Random(getattr(args, "seed", None))
        rnd.shuffle(chart_paths_random)
    except Exception:
        pass

    order = str(getattr(args, "order", "fixed") or "fixed").strip().lower()
    _log(f"order: {order}")
    chart_paths_main = chart_paths_fixed if order != "random" else chart_paths_random

    include_bg = (not bool(getattr(args, "no_bg", False)))
    include_bgm = (not bool(getattr(args, "no_bgm", False)))
    if bool(getattr(args, "include_bg", False)):
        include_bg = True
    if bool(getattr(args, "include_bgm", False)):
        include_bgm = True

    adv = build_advance_sequence(
        chart_paths=chart_paths_main,
        W=int(args.w),
        H=int(args.h),
        notes_per_chart=int(args.notes_per_chart),
        tail_time=float(args.tail_time),
        bgm_override=(str(args.bgm) if args.bgm else None),
        chart_speed=float(args.chart_speed),
        include_bg=bool(include_bg),
        include_bgm=bool(include_bgm),
        quiet=bool(getattr(args, "quiet", False)),
    )

    if getattr(args, "output_random", None):
        _log(f"write fixed: {str(args.output)}")
        _log(f"write random: {str(getattr(args, 'output_random'))}")
        adv_fixed = build_advance_sequence(
            chart_paths=chart_paths_fixed,
            W=int(args.w),
            H=int(args.h),
            notes_per_chart=int(args.notes_per_chart),
            tail_time=float(args.tail_time),
            bgm_override=(str(args.bgm) if args.bgm else None),
            chart_speed=float(args.chart_speed),
            include_bg=bool(include_bg),
            include_bgm=bool(include_bgm),
            quiet=True,
        )
        adv_random = build_advance_sequence(
            chart_paths=chart_paths_random,
            W=int(args.w),
            H=int(args.h),
            notes_per_chart=int(args.notes_per_chart),
            tail_time=float(args.tail_time),
            bgm_override=(str(args.bgm) if args.bgm else None),
            chart_speed=float(args.chart_speed),
            include_bg=bool(include_bg),
            include_bgm=bool(include_bgm),
            quiet=True,
        )
        _write_json(str(args.output), adv_fixed)
        _write_json(str(getattr(args, "output_random")), adv_random)
    else:
        _log(f"write: {str(args.output)}")
        _write_json(str(args.output), adv)


if __name__ == "__main__":
    main()
