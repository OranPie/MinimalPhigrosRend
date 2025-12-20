from __future__ import annotations

import json
import random
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple, Union

from ..types import RuntimeNote


_DT_SPEC = Union[int, float, Dict[str, Any]]


@dataclass
class JudgeAction:
    grade: Optional[str]
    dt_ms: float
    hold_percent: Optional[float]


@dataclass
class JudgeScriptMeta:
    index_mode: str
    require_total_notes: Optional[int]
    require_playable_notes: Optional[int]


@dataclass
class JudgeEntry:
    start: int
    end: int
    kind: str
    grade: Optional[str]
    dt_spec: _DT_SPEC
    hold_percent: Optional[float]


class JudgeScript:
    def __init__(self, meta: JudgeScriptMeta, entries: List[JudgeEntry]):
        self.meta = meta
        self.entries = entries


def _clamp01(x: float) -> float:
    if x < 0.0:
        return 0.0
    if x > 1.0:
        return 1.0
    return x


def _parse_dt_spec(v: Any) -> _DT_SPEC:
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, dict):
        return v
    raise ValueError("dt_ms must be number or object")


def _pick_dt_ms(rng: random.Random, spec: _DT_SPEC) -> float:
    if isinstance(spec, (int, float)):
        return float(spec)
    if not isinstance(spec, dict):
        return 0.0

    if "min" in spec and "max" in spec:
        return float(rng.uniform(float(spec["min"]), float(spec["max"])))

    if "values" in spec:
        vals = spec.get("values")
        wts = spec.get("weights")
        if not isinstance(vals, list) or not vals:
            return 0.0
        if wts is None:
            return float(rng.choice([float(x) for x in vals]))
        if not isinstance(wts, list) or len(wts) != len(vals):
            raise ValueError("dt_ms.weights must be same length as dt_ms.values")
        vals_f = [float(x) for x in vals]
        wts_f = [float(x) for x in wts]
        tot = sum(max(0.0, w) for w in wts_f)
        if tot <= 1e-9:
            return float(rng.choice(vals_f))
        r = rng.random() * tot
        acc = 0.0
        for x, w in zip(vals_f, wts_f):
            acc += max(0.0, w)
            if r <= acc:
                return float(x)
        return float(vals_f[-1])

    raise ValueError("dt_ms object must be {min,max} or {values[,weights]}")


def _kind_match(note_kind: int, entry_kind: str) -> bool:
    k = str(entry_kind or "any").lower()
    if k in ("any", "*"):
        return True
    if k == "tap":
        return int(note_kind) == 1
    if k == "drag":
        return int(note_kind) == 2
    if k == "hold":
        return int(note_kind) == 3
    if k == "flick":
        return int(note_kind) == 4
    return False


class JudgePlan:
    def __init__(self, actions_by_note_id: Dict[int, JudgeAction]):
        self._by_nid = actions_by_note_id

    def action_for(self, note: RuntimeNote) -> Optional[JudgeAction]:
        return self._by_nid.get(int(note.nid))


def load_judge_script(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        raw = f.read().lstrip("\ufeff")
    data = json.loads(raw)
    if not isinstance(data, dict):
        raise ValueError("judge_script root must be an object")
    return data


def parse_judge_script(data: Dict[str, Any]) -> JudgeScript:
    if int(data.get("version", 1)) != 1:
        raise ValueError("unsupported judge_script version")

    meta_in = data.get("meta")
    if not isinstance(meta_in, dict):
        meta_in = {}

    meta = JudgeScriptMeta(
        index_mode=str(meta_in.get("index_mode", "playable")),
        require_total_notes=(int(meta_in["require_total_notes"]) if meta_in.get("require_total_notes") is not None else None),
        require_playable_notes=(int(meta_in["require_playable_notes"]) if meta_in.get("require_playable_notes") is not None else None),
    )

    ents_in = data.get("entries")
    if not isinstance(ents_in, list):
        raise ValueError("entries must be a list")

    entries: List[JudgeEntry] = []
    for e in ents_in:
        if not isinstance(e, dict):
            continue
        st = int(e.get("startNoteIndex", 0))
        ed = int(e.get("endNoteIndex", st))
        kind = str(e.get("kind", "any"))
        grade = e.get("grade", None)
        grade_s = None if grade is None else str(grade).upper()
        dt_spec = _parse_dt_spec(e.get("dt_ms", 0))
        hp = e.get("holdPercent", None)
        hp_f = None
        if hp is not None:
            hp_f = _clamp01(float(hp))
        entries.append(JudgeEntry(start=st, end=ed, kind=kind, grade=grade_s, dt_spec=dt_spec, hold_percent=hp_f))

    return JudgeScript(meta=meta, entries=entries)


def build_judge_plan(
    script: JudgeScript,
    notes: List[RuntimeNote],
    *,
    seed: Optional[int] = None,
) -> JudgePlan:
    rng = random.Random(seed)

    total_notes = len(notes)
    playable_notes = [n for n in notes if not bool(getattr(n, "fake", False))]

    if script.meta.require_total_notes is not None and int(script.meta.require_total_notes) != int(total_notes):
        raise ValueError(f"judge_script total notes mismatch: script={int(script.meta.require_total_notes)} chart={int(total_notes)}")

    if script.meta.require_playable_notes is not None and int(script.meta.require_playable_notes) != int(len(playable_notes)):
        raise ValueError(
            f"judge_script playable notes mismatch: script={int(script.meta.require_playable_notes)} chart={int(len(playable_notes))}"
        )

    index_mode = str(script.meta.index_mode or "playable").lower()
    if index_mode not in ("playable", "all"):
        index_mode = "playable"

    if index_mode == "all":
        seq = list(notes)
    else:
        seq = list(playable_notes)

    seq.sort(key=lambda n: (float(getattr(n, "t_hit", 0.0)), int(getattr(n, "line_id", 0)), int(getattr(n, "nid", 0))))

    actions_by_nid: Dict[int, JudgeAction] = {}
    for idx, n in enumerate(seq):
        chosen: Optional[JudgeEntry] = None
        for ent in script.entries:
            if idx < int(ent.start) or idx > int(ent.end):
                continue
            if not _kind_match(int(getattr(n, "kind", 1)), str(ent.kind)):
                continue
            chosen = ent
        if chosen is None:
            continue

        dt_ms = _pick_dt_ms(rng, chosen.dt_spec)
        actions_by_nid[int(n.nid)] = JudgeAction(grade=chosen.grade, dt_ms=float(dt_ms), hold_percent=chosen.hold_percent)

    return JudgePlan(actions_by_note_id=actions_by_nid)
