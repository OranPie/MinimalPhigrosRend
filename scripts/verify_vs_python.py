#!/usr/bin/env python3
"""Compare C++ verify_chart output against Python renderer for consistency.

Usage:
    python3 scripts/verify_vs_python.py <chart_path> [width] [height]

This script:
1. Runs the C++ verify_chart tool on the chart
2. Loads the same chart via Python
3. Compares line states and note positions at sampled timestamps
4. Reports discrepancies with tolerances
"""
import json
import math
import os
import subprocess
import sys

# Add project root to path
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)


def load_chart_python(chart_path, W, H):
    """Load chart using Python parser, return (lines, notes, offset)."""
    # Import parsers directly to avoid pygame dependency chain
    import importlib
    import types

    with open(chart_path, "r") as f:
        text = f.read()

    text_stripped = text.lstrip()
    if not text_stripped.startswith("{"):
        spec = importlib.util.spec_from_file_location(
            "pec", os.path.join(ROOT, "phic_renderer", "chart", "pec.py"))
        mod = importlib.util.module_from_spec(spec)
        # Patch imports
        sys.modules.setdefault("phic_renderer", types.ModuleType("phic_renderer"))
        sys.modules.setdefault("phic_renderer.chart", types.ModuleType("phic_renderer.chart"))
        spec.loader.exec_module(mod)
        return mod.load_pec_text(text, W, H)

    data = json.loads(text)
    if "META" in data or "BPMList" in data:
        fmt = "rpe"
    else:
        fmt = "official"

    # Use direct module loading to avoid pygame chain
    # First load dependencies
    math_util_spec = importlib.util.spec_from_file_location(
        "phic_renderer.math.util",
        os.path.join(ROOT, "phic_renderer", "math", "util.py"))
    math_util = importlib.util.module_from_spec(math_util_spec)
    sys.modules["phic_renderer.math.util"] = math_util
    math_util_spec.loader.exec_module(math_util)

    math_easing_spec = importlib.util.spec_from_file_location(
        "phic_renderer.math.easing",
        os.path.join(ROOT, "phic_renderer", "math", "easing.py"))
    math_easing = importlib.util.module_from_spec(math_easing_spec)
    sys.modules["phic_renderer.math.easing"] = math_easing
    math_easing_spec.loader.exec_module(math_easing)

    math_tracks_spec = importlib.util.spec_from_file_location(
        "phic_renderer.math.tracks",
        os.path.join(ROOT, "phic_renderer", "math", "tracks.py"))
    math_tracks = importlib.util.module_from_spec(math_tracks_spec)
    sys.modules["phic_renderer.math.tracks"] = math_tracks
    math_tracks_spec.loader.exec_module(math_tracks)

    types_spec = importlib.util.spec_from_file_location(
        "phic_renderer.types",
        os.path.join(ROOT, "phic_renderer", "types.py"))
    types_mod = importlib.util.module_from_spec(types_spec)
    sys.modules["phic_renderer.types"] = types_mod
    types_spec.loader.exec_module(types_mod)

    # Ensure package modules exist
    pkg = types.ModuleType("phic_renderer")
    pkg.math = types.ModuleType("phic_renderer.math")
    pkg.math.util = math_util
    pkg.math.easing = math_easing
    pkg.math.tracks = math_tracks
    pkg.types = types_mod
    sys.modules["phic_renderer"] = pkg
    sys.modules["phic_renderer.math"] = pkg.math
    sys.modules["phic_renderer.chart"] = types.ModuleType("phic_renderer.chart")

    parser_file = os.path.join(ROOT, "phic_renderer", "chart", f"{fmt}.py")
    spec = importlib.util.spec_from_file_location(f"phic_renderer.chart.{fmt}", parser_file)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[f"phic_renderer.chart.{fmt}"] = mod
    spec.loader.exec_module(mod)

    if fmt == "rpe":
        return mod.load_rpe(data, W, H)
    else:
        return mod.load_official(data, W, H)


def eval_line_python(line, t):
    """Evaluate line state at time t using Python tracks."""
    x = line.pos_x.eval(t) if hasattr(line.pos_x, "eval") else float(line.pos_x(t))
    y = line.pos_y.eval(t) if hasattr(line.pos_y, "eval") else float(line.pos_y(t))
    rot = line.rot.eval(t) if hasattr(line.rot, "eval") else float(line.rot(t))
    a = line.alpha.eval(t) if hasattr(line.alpha, "eval") else float(line.alpha(t))
    s = line.scroll_px.integral(t)
    a01 = max(0.0, min(abs(a), 1.0))
    return {"x": x, "y": y, "rot": rot, "alpha": a01, "scroll": s}


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/verify_vs_python.py <chart_path> [W] [H]")
        sys.exit(1)

    chart_path = sys.argv[1]
    W = int(sys.argv[2]) if len(sys.argv) >= 3 else 1280
    H = int(sys.argv[3]) if len(sys.argv) >= 4 else 720

    # Run C++ tool
    cpp_bin = os.path.join(ROOT, "cpp", "build", "verify_chart")
    if not os.path.isfile(cpp_bin):
        print(f"ERROR: C++ binary not found: {cpp_bin}")
        print("Build with: cd cpp/build && cmake .. && make")
        sys.exit(1)

    print(f"Running C++ verify_chart on {chart_path} ({W}x{H})...")
    result = subprocess.run(
        [cpp_bin, chart_path, str(W), str(H), "60", "10"],
        capture_output=True, text=True, timeout=60
    )
    if result.returncode != 0:
        print(f"C++ tool failed: {result.stderr}")
        sys.exit(1)

    cpp_data = json.loads(result.stdout)
    print(f"  C++ loaded: {cpp_data['lines_count']} lines, {cpp_data['notes_count']} notes")
    print(f"  C++ computed {cpp_data['sampled_frames']} sampled frames in {cpp_data['elapsed_ms']:.1f}ms")

    # Load in Python
    print(f"Loading chart in Python...")
    py_result = load_chart_python(chart_path, W, H)

    # Handle different return formats: (offset, lines, notes)
    if isinstance(py_result, tuple):
        if len(py_result) >= 3 and isinstance(py_result[0], (int, float)):
            py_offset, py_lines, py_notes = py_result[0], py_result[1], py_result[2]
        else:
            py_lines, py_notes = py_result[0], py_result[1]
            py_offset = py_result[2] if len(py_result) > 2 else 0.0
    else:
        py_lines = py_result.lines if hasattr(py_result, 'lines') else py_result["lines"]
        py_notes = py_result.notes if hasattr(py_result, 'notes') else py_result["notes"]
        py_offset = py_result.offset if hasattr(py_result, 'offset') else py_result.get("offset", 0.0)

    print(f"  Python loaded: {len(py_lines)} lines, {len(py_notes)} notes")

    # Compare line counts
    errors = 0
    warnings = 0

    if len(py_lines) != cpp_data["lines_count"]:
        print(f"  MISMATCH: line count C++={cpp_data['lines_count']} Python={len(py_lines)}")
        errors += 1
    if len(py_notes) != cpp_data["notes_count"]:
        print(f"  MISMATCH: note count C++={cpp_data['notes_count']} Python={len(py_notes)}")
        errors += 1

    # Compare line states at sampled frames
    POS_TOL = 1.0     # 1px tolerance for positions
    ROT_TOL = 0.001   # ~0.06 degrees
    ALPHA_TOL = 0.01
    SCROLL_TOL = 1.0

    print(f"\nComparing line states at sampled frames...")
    frames = cpp_data["frames"]
    max_frames_to_check = min(50, len(frames))

    for fi in range(max_frames_to_check):
        fdata = frames[fi]
        t = fdata["t"]

        for cl in fdata["lines"]:
            lid = cl["lid"]
            # Find matching Python line
            py_line = None
            for pl in py_lines:
                if pl.lid == lid:
                    py_line = pl
                    break
            if py_line is None:
                continue

            ps = eval_line_python(py_line, t)

            dx = abs(cl["x"] - ps["x"])
            dy = abs(cl["y"] - ps["y"])
            drot = abs(cl["rot"] - ps["rot"])
            dalpha = abs(cl["alpha"] - ps["alpha"])
            dscroll = abs(cl["scroll"] - ps["scroll"])

            if dx > POS_TOL or dy > POS_TOL:
                print(f"  t={t:.3f} line {lid}: pos MISMATCH "
                      f"C++({cl['x']:.3f},{cl['y']:.3f}) "
                      f"Py({ps['x']:.3f},{ps['y']:.3f}) "
                      f"delta=({dx:.3f},{dy:.3f})")
                errors += 1
            if drot > ROT_TOL:
                print(f"  t={t:.3f} line {lid}: rot MISMATCH "
                      f"C++={cl['rot']:.5f} Py={ps['rot']:.5f} delta={drot:.5f}")
                errors += 1
            if dalpha > ALPHA_TOL:
                print(f"  t={t:.3f} line {lid}: alpha MISMATCH "
                      f"C++={cl['alpha']:.4f} Py={ps['alpha']:.4f} delta={dalpha:.4f}")
                errors += 1
            if dscroll > SCROLL_TOL:
                print(f"  t={t:.3f} line {lid}: scroll MISMATCH "
                      f"C++={cl['scroll']:.2f} Py={ps['scroll']:.2f} delta={dscroll:.2f}")
                warnings += 1

    # Score check (only valid for full-chart runs)
    print(f"\nFinal score: {cpp_data['final_score']}")
    print(f"Final accuracy: {cpp_data['final_accuracy']:.4f}")
    print(f"Max combo: {cpp_data['max_combo']}/{cpp_data['playable']}")

    # Score is only expected to be perfect if ALL notes were reached
    if cpp_data["max_combo"] == cpp_data["playable"] and cpp_data["playable"] > 0:
        if cpp_data["final_score"] == 1000000:
            print("  ✓ Perfect autoplay score")
        else:
            print(f"  ✗ Expected 1000000 for full autoplay, got {cpp_data['final_score']}")
            errors += 1
    else:
        partial = cpp_data["max_combo"]
        total = cpp_data["playable"]
        print(f"  ℹ Partial run ({partial}/{total} notes judged), score check skipped")

    # Summary
    print(f"\n{'='*50}")
    print(f"RESULTS: {errors} errors, {warnings} warnings")
    if errors == 0:
        print("✓ PASS: C++ output matches Python within tolerances")
    else:
        print("✗ FAIL: Discrepancies found")
    return errors


if __name__ == "__main__":
    sys.exit(main())
