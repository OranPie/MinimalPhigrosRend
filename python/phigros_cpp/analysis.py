from __future__ import annotations

from typing import Iterable


def _normalize_rows(rows: Iterable[dict]) -> list[dict]:
    return [dict(row) for row in rows]


def rows_to_numpy(rows: Iterable[dict]) -> dict:
    rows = _normalize_rows(rows)
    if not rows:
        return {}

    import numpy as np

    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)

    out: dict = {}
    for key in keys:
        values = [row.get(key) for row in rows]
        try:
            out[key] = np.asarray(values)
        except Exception:
            out[key] = np.asarray(values, dtype=object)
    return out


def rows_to_pandas(rows: Iterable[dict]):
    rows = _normalize_rows(rows)
    import pandas as pd

    return pd.DataFrame(rows)


__all__ = ["rows_to_numpy", "rows_to_pandas"]
