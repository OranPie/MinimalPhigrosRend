from __future__ import annotations

import json
from typing import Any, Dict, Optional, Tuple

from .i18n import normalize_lang, tr


def _strip_jsonc_comments(src: str) -> str:
    # Removes //, # and /* */ comments while preserving string literals.
    out: list[str] = []
    i = 0
    n = len(src)

    in_str = False
    str_quote = '"'
    escape = False

    in_line_comment = False
    in_block_comment = False

    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
                out.append(ch)
            i += 1
            continue

        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue

        if in_str:
            out.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == str_quote:
                in_str = False
            i += 1
            continue

        if ch in ("\"", "'"):
            in_str = True
            str_quote = ch
            out.append(ch)
            i += 1
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue

        if ch == "#":
            in_line_comment = True
            i += 1
            continue

        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue

        out.append(ch)
        i += 1

    return "".join(out)


def load_config_v2(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        raw = f.read()
    raw = raw.lstrip("\ufeff")
    data = json.loads(_strip_jsonc_comments(raw))
    if not isinstance(data, dict):
        raise ValueError("config root must be an object")
    return data


def _get_section(cfg: Dict[str, Any], key: str) -> Dict[str, Any]:
    v = cfg.get(key)
    return v if isinstance(v, dict) else {}


def flatten_config_v2(cfg: Dict[str, Any]) -> Tuple[Dict[str, Any], Optional[Dict[str, Any]]]:
    mods_cfg = cfg.get("mods") if isinstance(cfg.get("mods"), dict) else None

    flat: Dict[str, Any] = {}

    window = _get_section(cfg, "window")
    render = _get_section(cfg, "render")
    audio = _get_section(cfg, "audio")
    assets = _get_section(cfg, "assets")
    gameplay = _get_section(cfg, "gameplay")
    ui = _get_section(cfg, "ui")
    debug = _get_section(cfg, "debug")
    rpe = _get_section(cfg, "rpe")

    def pull(dst_key: str, section: Dict[str, Any], section_key: str):
        if section_key in section:
            flat[dst_key] = section.get(section_key)

    pull("w", window, "w")
    pull("h", window, "h")

    pull("backend", render, "backend")
    pull("approach", render, "approach")
    pull("chart_speed", render, "chart_speed")
    pull("no_cull", render, "no_cull")
    pull("no_cull_screen", render, "no_cull_screen")
    pull("no_cull_enter_time", render, "no_cull_enter_time")
    pull("expand", render, "expand")

    pull("note_scale_x", render, "note_scale_x")
    pull("note_scale_y", render, "note_scale_y")
    pull("note_flow_speed_multiplier", render, "note_flow_speed_multiplier")

    pull("overrender", render, "overrender")
    pull("trail_alpha", render, "trail_alpha")
    pull("trail_blur", render, "trail_blur")
    pull("trail_dim", render, "trail_dim")
    pull("trail_frames", render, "trail_frames")
    pull("trail_decay", render, "trail_decay")
    pull("trail_blend", render, "trail_blend")
    pull("trail_blur_ramp", render, "trail_blur_ramp")

    pull("hitfx_scale_mul", render, "hitfx_scale_mul")
    pull("multicolor_lines", render, "multicolor_lines")
    pull("no_note_outline", render, "no_note_outline")
    pull("line_alpha_affects_notes", render, "line_alpha_affects_notes")

    pull("audio_backend", audio, "audio_backend")
    pull("bgm", audio, "bgm")
    pull("force", audio, "force")
    pull("bgm_volume", audio, "bgm_volume")
    pull("hitsound_min_interval_ms", audio, "hitsound_min_interval_ms")

    pull("respack", assets, "respack")
    pull("bg", assets, "bg")
    pull("bg_blur", assets, "bg_blur")
    pull("bg_dim", assets, "bg_dim")

    pull("autoplay", gameplay, "autoplay")
    pull("hold_fx_interval_ms", gameplay, "hold_fx_interval_ms")
    pull("hold_tail_tol", gameplay, "hold_tail_tol")
    pull("start_time", gameplay, "start_time")
    pull("end_time", gameplay, "end_time")

    pull("no_title_overlay", ui, "no_title_overlay")
    pull("lang", ui, "lang")
    pull("font_path", ui, "font_path")
    pull("font_size_multiplier", ui, "font_size_multiplier")

    pull("debug_line_label", debug, "debug_line_label")
    pull("debug_line_stats", debug, "debug_line_stats")
    pull("debug_judge_windows", debug, "debug_judge_windows")
    pull("debug_note_info", debug, "debug_note_info")
    pull("debug_particles", debug, "debug_particles")
    pull("basic_debug", debug, "basic_debug")

    pull("rpe_easing_shift", rpe, "rpe_easing_shift")

    return flat, mods_cfg


def dump_config_v2(args: Any, *, mods: Optional[Dict[str, Any]] = None, lang: Optional[str] = None) -> str:
    lng = normalize_lang(lang or getattr(args, "lang", None))
    cfg: Dict[str, Any] = {
        "version": 2,
        "window": {
            "w": int(getattr(args, "w", 1280)),
            "h": int(getattr(args, "h", 720)),
        },
        "render": {
            "backend": getattr(args, "backend", "pygame"),
            "approach": float(getattr(args, "approach", 3.0)),
            "chart_speed": float(getattr(args, "chart_speed", 1.0)),
            "no_cull": bool(getattr(args, "no_cull", False)),
            "no_cull_screen": bool(getattr(args, "no_cull_screen", False)),
            "no_cull_enter_time": bool(getattr(args, "no_cull_enter_time", False)),
            "expand": float(getattr(args, "expand", 1.0)),
            "note_scale_x": float(getattr(args, "note_scale_x", 1.0)),
            "note_scale_y": float(getattr(args, "note_scale_y", 1.0)),
            "note_flow_speed_multiplier": float(getattr(args, "note_flow_speed_multiplier", 1.0)),
            "overrender": float(getattr(args, "overrender", 2.0)),
            "trail_alpha": float(getattr(args, "trail_alpha", 0.0)),
            "trail_blur": int(getattr(args, "trail_blur", 0)),
            "trail_dim": int(getattr(args, "trail_dim", 0)),
            "trail_frames": int(getattr(args, "trail_frames", 1)),
            "trail_decay": float(getattr(args, "trail_decay", 0.85)),
            "trail_blend": str(getattr(args, "trail_blend", "normal")),
            "trail_blur_ramp": bool(getattr(args, "trail_blur_ramp", False)),
            "hitfx_scale_mul": float(getattr(args, "hitfx_scale_mul", 1.0)),
            "multicolor_lines": bool(getattr(args, "multicolor_lines", False)),
            "no_note_outline": bool(getattr(args, "no_note_outline", False)),
            "line_alpha_affects_notes": getattr(args, "line_alpha_affects_notes", "negative_only"),
        },
        "audio": {
            "audio_backend": getattr(args, "audio_backend", "pygame"),
            "bgm": getattr(args, "bgm", None),
            "force": bool(getattr(args, "force", False)),
            "bgm_volume": float(getattr(args, "bgm_volume", 0.8)),
            "hitsound_min_interval_ms": int(getattr(args, "hitsound_min_interval_ms", 30)),
        },
        "assets": {
            "respack": getattr(args, "respack", None),
            "bg": getattr(args, "bg", None),
            "bg_blur": int(getattr(args, "bg_blur", 10)),
            "bg_dim": int(getattr(args, "bg_dim", 120)),
        },
        "gameplay": {
            "autoplay": bool(getattr(args, "autoplay", False)),
            "hold_fx_interval_ms": int(getattr(args, "hold_fx_interval_ms", 200)),
            "hold_tail_tol": float(getattr(args, "hold_tail_tol", 0.8)),
            "start_time": getattr(args, "start_time", None),
            "end_time": getattr(args, "end_time", None),
        },
        "ui": {
            "lang": lng,
            "no_title_overlay": bool(getattr(args, "no_title_overlay", False)),
            "font_path": getattr(args, "font_path", None),
            "font_size_multiplier": float(getattr(args, "font_size_multiplier", 1.0)),
        },
        "rpe": {
            "rpe_easing_shift": int(getattr(args, "rpe_easing_shift", 0)),
        },
        "debug": {
            "basic_debug": bool(getattr(args, "basic_debug", False)),
            "debug_line_label": bool(getattr(args, "debug_line_label", False)),
            "debug_line_stats": bool(getattr(args, "debug_line_stats", False)),
            "debug_judge_windows": bool(getattr(args, "debug_judge_windows", False)),
            "debug_note_info": bool(getattr(args, "debug_note_info", False)),
            "debug_particles": bool(getattr(args, "debug_particles", False)),
        },
    }

    if isinstance(mods, dict):
        cfg["mods"] = mods

    if lng == "zh-CN":
        header_lines = [
            "// MinimalPhigrosRend 配置 v2（支持注释的 JSON）",
            "//",
            "// 基本用法：",
            "//   python3 -m phic_renderer --input <chart_or_pack> --config <this_file>",
            "//   python3 -m phic_renderer --input <chart_or_pack> --save_config config.jsonc",
            "//",
            "// 说明：",
            "// - 以 // 或 # 开头的行会被当作注释忽略。",
            "// - 命令行参数优先级高于配置文件。",
            "",
        ]
    else:
        header_lines = [
            "// MinimalPhigrosRend config v2 (JSON with comments)",
            "//",
            "// Basic usage:",
            "//   python3 -m phic_renderer --input <chart_or_pack> --config <this_file>",
            "//   python3 -m phic_renderer --input <chart_or_pack> --save_config config.jsonc",
            "//",
            "// Notes:",
            "// - Lines starting with // or # are comments.",
            "// - CLI args override config values.",
            "",
        ]

    header = "\n".join(header_lines)

    body = json.dumps(cfg, ensure_ascii=False, indent=2)
    return header + body + "\n"
