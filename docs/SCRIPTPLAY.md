# ScriptPlay DSL

`scriptplay` is a deterministic judgment script format for `phigros_render`.

Use it when you want something between perfect autoplay and manual play:

- force exact note windows by filter
- script hold release progress with percent or milliseconds
- run the same plan in live mode and `--score-only`

## Run

```bash
./cpp/build/phigros_render charts/MyChart/IN.json \
  --score-only \
  --mode scriptplay \
  --scriptplay docs/scriptplay_template.json
```

You can also set it in config:

```json
{
  "gameplay": {
    "mode": "scriptplay",
    "judge_script": "docs/scriptplay_template.json"
  }
}
```

## Shape

```json
{
  "version": 1,
  "meta": {
    "name": "mixed test",
    "index_mode": "playable",
    "require_playable_notes": 1600
  },
  "entries": [
    {
      "filter": { "kind": "hold" },
      "judge": { "grade": "GOOD", "dt_ms": 60 },
      "hold": { "percent": 1.0 }
    },
    {
      "filter": { "noteIndexes": [0, 1, 2] },
      "judge": { "grade": "MISS" }
    }
  ]
}
```

## Rules

- Entries apply in order. Later entries override earlier values on matched notes.
- Unmatched playable notes default to autoplay-style `PERFECT` with `dt_ms = 0`.
- `judge.grade` is the hit grade for tap/drag/flick notes, and the hold-start grade for hold notes.
- Hold final result still follows engine rules:
  `hold.percent` or `hold.ms` below `hold_tail_tol` yields a miss on release.
- `judge.grade = "MISS"` means “do not hit this note”.

## Filters

`filter` supports:

- `startNoteIndex`, `endNoteIndex`
- `noteIndexes`
- `noteIds`
- `lineIds`
- `kind` or `kinds`
- `above`
- `fake`
- `mh`
- `playable`

`index_mode` controls what `noteIndexes` mean:

- `playable`: count only non-fake notes
- `all`: count all notes

## Value Forms

`dt_ms`, `hold.percent`, and `hold.ms` accept:

- fixed number: `60`
- range: `{ "min": -40, "max": 40 }`
- deterministic sequence:
  `{ "values": [-20, 0, 20], "weights": [1, 4, 1] }`

Ranges are spread across matched notes. Sequences cycle deterministically.

## Compatibility Aliases

Older field names still parse:

- `grade`, `dt_ms`, `holdPercent`, `holdMs`
- top-level `kind`, `startNoteIndex`, `endNoteIndex`

For a ready-to-edit example, see [scriptplay_template.json](scriptplay_template.json).
