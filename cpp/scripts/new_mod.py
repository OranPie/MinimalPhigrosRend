#!/usr/bin/env python3
"""
new_mod.py — Phigros chart mod generator.

Creates a .mod.json file interactively or from command-line arguments.

Usage:
  Interactive:   python3 scripts/new_mod.py
  With args:     python3 scripts/new_mod.py [--name NAME] [--out FILE] OP [OP ...]

  Each OP is one of the templates below (or 'list' to list them).

Examples:
  python3 scripts/new_mod.py mirror
  python3 scripts/new_mod.py --name "My Mod" --out my.mod.json mirror rainbow
  python3 scripts/new_mod.py list
"""

import argparse
import json
import os
import sys
from pathlib import Path

# ── Op templates ─────────────────────────────────────────────────────────────
# Each entry: (display_name, description, default_json)
OP_TEMPLATES: dict[str, tuple[str, str, dict]] = {
    "mirror": (
        "Mirror",
        "Flip all note x positions around center (0 = chart center).",
        {"type": "mirror", "center": 0.0, "flip_side": False},
    ),
    "rainbow": (
        "Rainbow (colorize hue)",
        "Cycle full HSV spectrum over the chart's timeline.",
        {"type": "colorize", "mode": "hue", "hue_s": 1.0, "hue_v": 1.0},
    ),
    "gradient": (
        "Gradient (colorize gradient)",
        "Linearly interpolate note color from 'from' to 'to' over time.",
        {"type": "colorize", "mode": "gradient",
         "from": [255, 100, 100], "to": [100, 100, 255]},
    ),
    "constant_color": (
        "Constant color (colorize)",
        "Set all note tints to a single RGB color.",
        {"type": "colorize", "mode": "constant", "color": [255, 200, 80]},
    ),
    "kind_colors": (
        "Kind colors (colorize by_kind)",
        "Different color per note type: tap/drag/hold/flick.",
        {"type": "colorize", "mode": "by_kind",
         "by_kind": {"1": [220, 220, 220], "2": [80, 220, 255],
                     "3": [255, 220, 60],  "4": [255, 80, 80]}},
    ),
    "speed": (
        "Speed multiplier",
        "Multiply every note's scroll speed (e.g. 1.5 = faster approach).",
        {"type": "speed", "mul": 1.5},
    ),
    "opacity": (
        "Opacity",
        "Set all note alpha (0.0–1.0). 0.5 = semi-transparent.",
        {"type": "opacity", "alpha": 0.75},
    ),
    "wave": (
        "Wave",
        "Sinusoidal x displacement — notes oscillate left/right.",
        {"type": "wave", "amplitude": 100.0, "frequency": 1.0, "phase": 0.0},
    ),
    "shuffle": (
        "Shuffle",
        "Randomly displace note x positions (seeded for reproducibility).",
        {"type": "shuffle", "seed": 42, "range": 200.0},
    ),
    "taps_only": (
        "Training: Taps + Flicks only",
        "Remove drag and hold notes (keep kinds 1 and 4).",
        {"type": "note_filter", "keep": [1, 4]},
    ),
    "no_holds": (
        "Training: Remove holds",
        "Remove hold notes only (remove kind 3).",
        {"type": "note_filter", "remove": [3]},
    ),
    "flip_timing": (
        "Flip timing",
        "Reverse the temporal order of note hit times.",
        {"type": "flip_timing"},
    ),
    "scale": (
        "Scale positions",
        "Multiply note x and/or y positions by a factor.",
        {"type": "scale", "x_mul": 1.0, "y_mul": 1.0},
    ),
    "chaos": (
        "Chaos (mirror + wave + shuffle + rainbow)",
        "Combine mirror, wave, shuffle, and rainbow into one mod.",
        None,  # special: composite
    ),
}

CHAOS_OPS = [
    {"type": "mirror"},
    {"type": "wave", "amplitude": 80, "frequency": 0.8},
    {"type": "shuffle", "seed": 999, "range": 100},
    {"type": "colorize", "mode": "hue"},
]


# ── Helpers ───────────────────────────────────────────────────────────────────

def list_templates() -> None:
    print("\nAvailable mod op templates:\n")
    col = 22
    for key, (name, desc, _) in OP_TEMPLATES.items():
        print(f"  {key:<{col}}  {name}")
        print(f"  {'':<{col}}  {desc}\n")


def expand_ops(template_keys: list[str]) -> list[dict]:
    ops = []
    for key in template_keys:
        if key not in OP_TEMPLATES:
            sys.exit(f"[Error] Unknown op template '{key}'. Run with 'list' to see options.")
        _, _, default_json = OP_TEMPLATES[key]
        if default_json is None:  # composite
            ops.extend(CHAOS_OPS)
        else:
            ops.append(dict(default_json))
    return ops


def prompt(msg: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    val = input(f"{msg}{suffix}: ").strip()
    return val if val else default


def interactive_build() -> tuple[str, str, list[dict]]:
    print("\n── Phigros new_mod.py ───────────────────────────────────────────")
    print("Creates a .mod.json file. Press Enter to keep defaults.\n")

    name = prompt("Mod name", "My Mod")
    desc = prompt("Description (optional)", "")

    print("\nAvailable op templates (type 'list' to see all):")
    print("  " + "  ".join(OP_TEMPLATES.keys()))

    raw = prompt("\nOps to include (space-separated)", "mirror")
    keys = raw.split()
    if "list" in keys:
        list_templates()
        raw = prompt("Ops to include (space-separated)", "mirror")
        keys = raw.split()

    ops = expand_ops(keys)
    return name, desc, ops


def write_mod(path: str, name: str, desc: str, ops: list[dict]) -> None:
    mod = {"name": name, "ops": ops}
    if desc:
        mod = {"name": name, "description": desc, "ops": ops}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(mod, f, indent=2)
        f.write("\n")
    print(f"\n[OK] Written: {path}")
    print(f"     {len(ops)} op{'s' if len(ops) != 1 else ''}: " +
          ", ".join(op["type"] for op in ops))
    print(f"\nUsage: phigros_render <chart> --mod {path}")


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    # Quick 'list' shortcut
    if len(sys.argv) == 2 and sys.argv[1] == "list":
        list_templates()
        return

    parser = argparse.ArgumentParser(
        description="Generate a Phigros .mod.json file.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("ops", nargs="*", metavar="OP",
                        help="Op template names (run 'list' to see all)")
    parser.add_argument("--name", default="", help="Mod name")
    parser.add_argument("--description", "--desc", default="", help="Mod description")
    parser.add_argument("--out", default="", metavar="FILE",
                        help="Output file path (default: <name>.mod.json)")
    args = parser.parse_args()

    if "list" in args.ops:
        list_templates()
        return

    # Non-interactive mode if ops are provided on CLI
    if args.ops:
        name = args.name or " + ".join(
            OP_TEMPLATES[k][0] if k in OP_TEMPLATES else k for k in args.ops
        )
        desc = args.description
        ops = expand_ops(args.ops)
    else:
        # Interactive
        name, desc, ops = interactive_build()

    # Determine output path
    if args.out:
        out_path = args.out
    else:
        safe = name.lower().replace(" ", "_").replace("/", "_")
        out_path = f"{safe}.mod.json"

    # Confirm overwrite
    if os.path.exists(out_path):
        ans = input(f"'{out_path}' already exists. Overwrite? [y/N] ").strip().lower()
        if ans != "y":
            print("Aborted.")
            return

    write_mod(out_path, name, desc, ops)


if __name__ == "__main__":
    main()
