# Python vs C++/Web Alignment Notes

Checked against `phic_renderer` (Python) and `phic_port` + `phic_web`.

## Aligned now

- Judge windows and weights are consistent (`PERFECT=0.045`, `GOOD=0.090`, `BAD=0.150`, weights `1.0/0.6/0/0`).
- Internal note-kind IDs are consistent (`tap=1, drag=2, hold=3, flick=4`).
- JSON type parsing is format-aware:
  - Official-like: `1 tap, 2 drag, 3 hold, 4 flick`
  - RPE: `1 tap, 2 hold, 3 flick, 4 drag` -> remapped to internal IDs
- Parser timing semantics improved:
  - Official note times use per-line BPM unit conversion (`1.875 / bpm`)
  - RPE beat-time arrays use BPM map + `bpmfactor`
- C API ABI v5 adds parity payloads:
  - `phic_engine_step_v2` / `phic_engine_step_auto_v2`
  - judge events now include `note_kind`
  - frame commands include `t_hit_sec` and `hold_end_sec`
- Cross-implementation parity oracle test added:
  - `phic_parity_oracle` (`tests/oracle_compare.py`) compares Python parser+mods outputs vs C++ core output dump.
- C++ mod surface expanded:
  - `full_blue` (non-hold to tap), lane `scale`, and `compress_zip` duplication support.
  - `attach` subset (lane/time offsets, side control, filter).
  - `fade` subset (time + constant alpha with filter).
  - `note_rules`/`note_overrides` subset (`kind`/`speed_mul`/`alpha`/`side`, with filter support).
  - stutter alpha-decay parity (`alpha_decay` / `opacity_decay`).

## Current gaps

- Python parser coverage is still broader (Official/RPE/PEC details, richer line/event decoding).
- C++ parser still simplifies many line/event tracks (especially RPE/PEC line dynamics).
- Python mod pipeline still supports a wider mod/filter surface (x/y/size/tint-focused transforms, line rules, and advanced variants).
- Web runtime keeps a legacy fallback path for old ABI payloads.

## Suggested follow-ups

- Port Python RPE timing/BPM conversion logic into C++ parser.
- Extend C++ mod coverage to match Python mod modules.
- Expand parser/event parity to include full line tracks and richer per-note metadata where behavior depends on them.
