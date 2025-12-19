from __future__ import annotations

from typing import Any, Dict, Optional, Tuple


def compute_score(acc_sum: float, judged_cnt: int, combo: int, max_combo: int, total_notes: int) -> Tuple[int, float, float]:
    acc_ratio = (acc_sum / total_notes) if total_notes > 0 else 0.0
    combo_ratio = (max_combo / total_notes) if total_notes > 0 else 0.0
    score = int((acc_ratio * 900000.0) + (combo_ratio * 100000.0))
    return score, acc_ratio, combo_ratio


def format_title(chart_info: Dict[str, Any]) -> Tuple[str, str]:
    nm = str(chart_info.get("name", ""))
    lv = str(chart_info.get("level", ""))
    diff = chart_info.get("difficulty", None)
    diff_s = f"{float(diff):.1f}" if diff is not None else ""
    title = f"{nm}".strip()
    sub = f"{lv}  ({diff_s})".strip() if (lv or diff_s) else ""
    return title, sub


def progress_ratio(t: float, chart_end: float, *, advance_active: bool, start_time: Optional[float]) -> float:
    if chart_end <= 1e-9:
        return 0.0
    display_t = t
    display_end = chart_end
    if (not advance_active) and (start_time is not None):
        display_t = t - float(start_time)
        display_end = chart_end - float(start_time)
    if display_end <= 1e-9:
        return 0.0
    r = display_t / display_end
    if r < 0.0:
        return 0.0
    if r > 1.0:
        return 1.0
    return float(r)
