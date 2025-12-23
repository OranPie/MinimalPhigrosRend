from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, apply_note_set, match_line_filter, apply_line_set


def apply_note_rules(mods_cfg: Dict[str, Any], notes: List[RuntimeNote]):
    rules_raw = mods_cfg.get("note_rules", mods_cfg.get("rules", None))
    if isinstance(rules_raw, list) and rules_raw:
        for rule in rules_raw:
            if not isinstance(rule, dict):
                continue
            flt = rule.get("filter", rule.get("when", {}))
            st = rule.get("set", rule.get("then", {}))
            if not isinstance(flt, dict) or not isinstance(st, dict):
                continue
            apply_to_hold = bool(rule.get("apply_to_hold", True))
            for n in notes:
                if (not apply_to_hold) and n.kind == 3:
                    continue
                if match_note_filter(n, flt):
                    apply_note_set(n, st)

    glob_no = mods_cfg.get("note_overrides", None)
    if isinstance(glob_no, dict) and glob_no:
        apply_to_hold = bool(glob_no.get("apply_to_hold", True))
        st = dict(glob_no.get("set", glob_no))
        for n in notes:
            if (not apply_to_hold) and n.kind == 3:
                continue
            apply_note_set(n, st)


def apply_line_rules(mods_cfg: Dict[str, Any], lines: List[RuntimeLine]):
    lr_raw = mods_cfg.get("line_rules", None)
    if isinstance(lr_raw, list) and lr_raw:
        for rule in lr_raw:
            if not isinstance(rule, dict):
                continue
            flt = rule.get("filter", rule.get("when", {}))
            st = rule.get("set", rule.get("then", {}))
            if not isinstance(flt, dict) or not isinstance(st, dict):
                continue
            for ln in lines:
                if match_line_filter(ln, flt):
                    apply_line_set(ln, st)
