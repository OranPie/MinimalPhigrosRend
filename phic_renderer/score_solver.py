from __future__ import annotations

import argparse
import json
import os
from typing import Any, Dict, List, Optional, Tuple

from .io.chart_loader import load_chart


def _sorted_playable(notes):
    seq = [n for n in notes if not bool(getattr(n, "fake", False))]
    seq.sort(key=lambda n: (float(getattr(n, "t_hit", 0.0)), int(getattr(n, "line_id", 0)), int(getattr(n, "nid", 0))))
    return seq


def _score_formula(*, T: int, M: int, P: int, G: int) -> int:
    if T <= 0:
        return 0
    acc_sum = float(P) + 0.6 * float(G)
    return int((acc_sum / float(T)) * 900000.0 + (float(M) / float(T)) * 100000.0)


def _find_counts(T: int, target: int) -> Tuple[int, int, int, int, int]:
    best = None
    for M in range(0, T + 1):
        for G in range(0, M + 1):
            P = M - G
            s = _score_formula(T=T, M=M, P=P, G=G)
            diff = abs(int(s) - int(target))
            cand = (diff, M, P, G, s)
            if best is None or cand < best:
                best = cand
            if diff == 0:
                return M, P, G, s, diff
    assert best is not None
    diff, M, P, G, s = best
    return M, P, G, s, diff


def _simulate_score(sequence: List[str]) -> Tuple[int, int, int, int]:
    T = len(sequence)
    max_combo = 0
    current_combo = 0
    P = G = 0
    for grade in sequence:
        if grade != "MISS":
            current_combo += 1
            max_combo = max(max_combo, current_combo)
            if grade == "PERFECT":
                P += 1
            elif grade == "GOOD":
                G += 1
        else:
            current_combo = 0
    M = max_combo
    return M, P, G, _score_formula(T=T, M=M, P=P, G=G)


def _build_sequence(T: int, M: int, G: int, breaks: int = 0) -> List[str]:
    if breaks == 0:
        seq = ["PERFECT"] * M + ["MISS"] * (T - M)
        for i in range(min(G, M)):
            seq[i] = "GOOD"
        return seq
    
    seq = ["MISS"] * T
    segment_len = M // (breaks + 1)
    remaining = M % (breaks + 1)
    idx = 0
    for seg in range(breaks + 1):
        length = segment_len + (1 if seg < remaining else 0)
        for i in range(length):
            if idx < T:
                seq[idx] = "PERFECT"
                idx += 1
        if seg < breaks:
            idx += 1
    for i in range(min(G, M)):
        if seq[i] != "MISS":
            seq[i] = "GOOD"
    return seq


def _optimize_for_target(T: int, target: int, max_breaks: int = 3) -> Tuple[int, int, int, List[str], int]:
    best = None
    for breaks in range(max_breaks + 1):
        for M in range(0, T + 1):
            for G in range(0, min(M, T) + 1):
                seq = _build_sequence(T, M, G, breaks)
                M_sim, P_sim, G_sim, score = _simulate_score(seq)
                diff = abs(score - target)
                cand = (diff, M_sim, P_sim, G_sim, seq, score)
                if best is None or cand < best:
                    best = cand
                if diff == 0:
                    return M_sim, P_sim, G_sim, seq, score
    assert best is not None
    diff, M_sim, P_sim, G_sim, seq, score = best
    return M_sim, P_sim, G_sim, seq, score


def _pick_good_indices(notes_sorted: List[Any], M: int, G: int) -> List[int]:
    if G <= 0:
        return []
    idxs: List[int] = []
    for i in range(min(M, len(notes_sorted))):
        k = int(getattr(notes_sorted[i], "kind", 1))
        if k in (1, 3):
            idxs.append(int(i))
            if len(idxs) >= G:
                break
    return idxs


def _build_script_from_sequence(notes_sorted: List[Any], sequence: List[str]) -> Dict[str, Any]:
    entries: List[Dict[str, Any]] = []
    
    for i, grade in enumerate(sequence):
        if grade == "MISS":
            entries.append({
                "startNoteIndex": i,
                "endNoteIndex": i,
                "kind": "any",
                "grade": "MISS",
                "dt_ms": 0,
                "holdPercent": None,
            })
        elif grade == "PERFECT":
            entries.append({
                "startNoteIndex": i,
                "endNoteIndex": i,
                "kind": "any",
                "grade": "PERFECT",
                "dt_ms": 0,
                "holdPercent": 1.0,
            })
        elif grade == "GOOD":
            entries.append({
                "startNoteIndex": i,
                "endNoteIndex": i,
                "kind": "any",
                "grade": "GOOD",
                "dt_ms": 0,
                "holdPercent": 1.0,
            })
    
    return {
        "version": 1,
        "meta": {
            "index_mode": "playable",
            "require_total_notes": 0,
            "require_playable_notes": len(sequence),
        },
        "entries": entries,
    }


def _build_script(*, total_notes: int, playable_notes: int, good_indices: List[int], M: int) -> Dict[str, Any]:
    entries: List[Dict[str, Any]] = []

    entries.append(
        {
            "startNoteIndex": 0,
            "endNoteIndex": max(0, playable_notes - 1),
            "kind": "any",
            "grade": "MISS",
            "dt_ms": 0,
            "holdPercent": None,
        }
    )

    if M > 0:
        entries.append(
            {
                "startNoteIndex": 0,
                "endNoteIndex": int(M - 1),
                "kind": "any",
                "grade": "PERFECT",
                "dt_ms": 0,
                "holdPercent": 1.0,
            }
        )

    for gi in good_indices:
        entries.append(
            {
                "startNoteIndex": int(gi),
                "endNoteIndex": int(gi),
                "kind": "any",
                "grade": "GOOD",
                "dt_ms": 0,
                "holdPercent": 1.0,
            }
        )

    return {
        "version": 1,
        "meta": {
            "index_mode": "playable",
            "require_total_notes": int(total_notes),
            "require_playable_notes": int(playable_notes),
        },
        "entries": entries,
    }


def main():
    ap = argparse.ArgumentParser(prog="phic_renderer.score_solver")
    ap.add_argument("--chart", type=str, required=True, help="Chart path (.json/.pec/pack entry json)")
    ap.add_argument("--w", type=int, default=1280)
    ap.add_argument("--h", type=int, default=720)
    ap.add_argument("--target", type=int, required=True, help="Target realtime score")
    ap.add_argument("--out", type=str, default="judge_script.generated.json")
    ap.add_argument("--max_breaks", type=int, default=3, help="Maximum combo breaks to allow (more realistic patterns)")

    args = ap.parse_args()

    fmt, offset, lines, notes = load_chart(str(args.chart), int(args.w), int(args.h))
    playable = _sorted_playable(notes)

    T = int(len(playable))
    if T <= 0:
        raise SystemExit("No playable notes")

    M, P, G, seq, score = _optimize_for_target(T, int(args.target), max_breaks=int(args.max_breaks))
    diff = abs(int(score) - int(args.target))

    script = _build_script_from_sequence(playable, seq)
    script["meta"]["require_total_notes"] = len(notes)
    script["meta"]["require_playable_notes"] = len(playable)

    out_p = str(args.out)
    out_dir = os.path.dirname(os.path.abspath(out_p))
    if out_dir and (not os.path.exists(out_dir)):
        os.makedirs(out_dir, exist_ok=True)

    with open(out_p, "w", encoding="utf-8") as f:
        json.dump(script, f, ensure_ascii=False, indent=2)

    print(f"T={T}  M={M}  P={P}  G={G}  score={score}  diff={diff}  out={out_p}")
    print(f"Sequence preview: {''.join(seq[:30])}{'...' if len(seq) > 30 else ''}")
    print(f"Actual max combo from sequence: {M}")
    print(f"Breaks in sequence: {seq.count('MISS') - (T - M)}")


if __name__ == "__main__":
    main()
