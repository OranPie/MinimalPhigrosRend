# ChartScript — Declarative Chart Playlist DSL

ChartScript is the playlist scripting system built into `phigros_render`.
It replaces the Python `advance.json` / `gen_advance_from_charts.py` workflow
with a unified JSON-based DSL that is more expressive, fully typed, and runs
entirely inside the C++ renderer.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Top-Level Fields](#top-level-fields)
3. [Items](#items)
4. [Segments](#segments)
5. [Notes Window](#notes-window)
6. [on_complete — Post-Play Action](#on_complete--post-play-action)
7. [Config Overrides](#config-overrides)
8. [Inline Mods](#inline-mods)
9. [Groups](#groups)
10. [Variables](#variables)
11. [Presets](#presets)
12. [Filter](#filter)
13. [Global Filter](#global-filter)
14. [Discover (Auto-Discovery)](#discover-auto-discovery)
15. [Resume State](#resume-state)
16. [Python Generator — gen_chartscript.py](#python-generator--gen_chartscriptpy)
17. [Complete Example](#complete-example)

---

## Quick Start

```bash
phigros_render --script my_playlist.chartscript.json
```

Minimal playlist that plays two charts in sequence:

```jsonc
{
  "version": 2,
  "name": "My Playlist",
  "mode": "sequence",
  "items": [
    { "input": "charts/song_a/AT.json", "name": "Song A" },
    { "input": "charts/song_b/IN.json", "name": "Song B", "end": 60.0 }
  ]
}
```

---

## Top-Level Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `version` | int | 2 | Schema version (use `2`) |
| `name` | string | — | Human-readable playlist name |
| `mode` | string | `"sequence"` | `"sequence"` / `"shuffle"` / `"loop"` |
| `shuffle_seed` | int | 0 | Shuffle seed (0 = random each run) |
| `repeat` | int | 1 | Full passes before stopping (0 = infinite) |
| `discover_limit` | int | -1 | Cap total items from `discover` (-1 = no limit) |
| `resume_file` | string | — | Path to save/restore cursor position |
| `transition` | object | none | Transition between charts |
| `defaults` | object | — | Config applied to every item (overridden per-item) |
| `global_filter` | object | — | Filter applied to all items after discovery |
| `variables` | object | — | Variable map for `$name` substitution |
| `presets` | object | — | Named config shortcuts |
| `groups` | object | — | Named shared config+mods blocks |
| `discover` | object | — | Auto-discover charts from a directory |
| `items` | array | — | Explicit item list |

### Modes

| Mode | Description |
|------|-------------|
| `"sequence"` | Play items in listed order, once per pass |
| `"shuffle"` | Weighted random order per pass (re-shuffled each repeat) |
| `"loop"` | Sequence, but loop indefinitely until `Esc` |

### Transition

```jsonc
"transition": {
  "type": "fade",       // "none" | "fade" | "crossfade"
  "duration": 0.5       // seconds
}
```

---

## Items

Each entry in `"items"` describes one chart to play.

```jsonc
{
  "input":    "charts/song/AT.json",  // chart file path (required)
  "name":     "Song Title",           // display name
  "level":    "AT",                   // difficulty label (used for filters)
  "bgm":      "song.ogg",             // override BGM (optional)
  "bg":       "bg.jpg",               // override background (optional)

  "start":    0.0,                    // start time in chart (seconds)
  "end":      60.0,                   // end time (-1 = chart end)
  "start_at": 0.0,                    // time offset on main timeline

  "notes_window": 200,                // auto-compute end from first 200 notes
  "tail_time":    0.8,                // seconds after last note in window

  "segments": [ ... ],                // multiple windows (overrides start/end)

  "weight":   2,                      // relative probability in shuffle mode
  "group":    "hype",                 // inherit from a named group
  "tags":     ["boss", "fast"],       // tags used by filters and groups

  "config":   { "chart_speed": 1.5 },// per-item config overrides
  "mods":     [ ... ],                // inline mods
  "mod_file": "my.mod.json",          // external mod file

  "filter":   { "min_notes": 300 },   // skip this item if filter fails

  "on_complete": { ... }              // what to do after this item ends
}
```

### Field Descriptions

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `input` | string | required | Chart file path |
| `name` | string | — | Display name |
| `level` | string | — | Difficulty label (`AT`, `IN`, `HD`, `EZ`, etc.) |
| `bgm` | string | — | Override BGM path |
| `bg` | string | — | Override background image |
| `start` | float | 0.0 | Start time (seconds into chart) |
| `end` | float | -1 | End time (-1 = chart end + 2 s padding) |
| `start_at` | float | 0.0 | Offset on the main playlist timeline |
| `notes_window` | int | -1 | Auto-compute end from first N notes |
| `tail_time` | float | 0.5 | Seconds of padding after the last note in the window |
| `segments` | array | — | Multiple time windows (overrides `start`/`end`) |
| `weight` | int | 1 | Shuffle weight (higher = selected more often) |
| `group` | string | — | Inherit config+mods from this named group |
| `tags` | array | — | String tags for filter matching |
| `config` | object | — | Per-item render config overrides |
| `mods` | array | — | Inline mod operations |
| `mod_file` | string | — | External `.mod.json` file to apply |
| `filter` | object | — | Skip this item if filter does not match |
| `on_complete` | object | — | Action after this item finishes |

---

## Segments

Use `segments` when you want to play multiple non-contiguous windows from the same chart
without duplicating the item entry.  When `segments` is non-empty, `start`/`end` at the
item level are ignored.

```jsonc
"segments": [
  { "start": 0.0,  "end": 20.0 },
  { "start": 60.0, "end": 80.0, "notes_window": 50, "tail_time": 1.0 }
]
```

Each segment object:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `start` | float | 0.0 | Segment start (seconds) |
| `end` | float | -1 | Segment end (-1 = chart end) |
| `notes_window` | int | -1 | Auto-compute end from first N notes within segment |
| `tail_time` | float | 0.5 | Seconds after last note in window |

---

## Notes Window

`notes_window` lets you specify how many notes to include rather than a hard end time.
The renderer sorts all hit times in the chart and automatically sets the end to:

```
end = time_of_nth_note + tail_time
```

This mirrors the Python `gen_advance_from_charts.py --notes_per_chart` behaviour.

```jsonc
{
  "input": "charts/hard_chart/AT.json",
  "notes_window": 300,    // play up to and including the 300th note
  "tail_time": 1.0        // + 1 second of breathing room
}
```

---

## on_complete — Post-Play Action

`on_complete` controls what happens after an item (or all its segments) finish playing.

```jsonc
"on_complete": {
  "action": "next"         // default: advance to next item
}
```

### Actions

| Action | Description |
|--------|-------------|
| `"next"` | Advance to the next item in the list |
| `"repeat"` | Play the same item again immediately |
| `"loop"` | Jump back to item 0 |
| `"stop"` | End the playlist |
| `"goto"` | Jump to a specific item index (0-based) |

For `"goto"`, also set `"goto": <index>`:

```jsonc
"on_complete": { "action": "goto", "goto": 3 }
```

### Score-Conditional Branching

If `min_score` is set, the action chosen depends on whether the score cleared the threshold:

```jsonc
"on_complete": {
  "min_score":   900000,   // if score >= 900000:
  "action":      "next",   //   advance to next
  "else_action": "repeat"  // else: retry this chart
}
```

This lets you build skill-gated playlists — the player must pass a chart to move on.

---

## Config Overrides

Per-item `config` objects override the global render config for that item only.
The field names are the same as in the JSONC config file (under `"render"`, `"assets"`, etc.),
but written flat at the top level of the config object.

```jsonc
"config": {
  "chart_speed": 1.5,
  "trail_alpha": 0.7,
  "trail_chromatic": 2.0,
  "trail_glow": 0.4,
  "motion_blur_samples": 6,
  "motion_blur_shutter": 0.6,
  "motion_blur_curve": "gaussian",
  "bg_dim": 80
}
```

You can also reference a preset by name (see [Presets](#presets)):

```jsonc
"config": { "preset": "vibrant", "chart_speed": 1.8 }
```

Item-level values override preset values. Preset values override `defaults`.

---

## Inline Mods

The `mods` array accepts the same mod operations as `.mod.json` files, written inline.

```jsonc
"mods": [
  { "type": "colorize", "mode": "hue", "hue_s": 0.9, "hue_v": 0.85 },
  { "type": "mirror" },
  { "type": "speed",    "mul": 1.2 }
]
```

Supported mod types: `mirror`, `colorize`, `speed`, `opacity`, `wave`, `shuffle`,
`note_filter`, `flip_timing`, `scale`.

You can also load mods from a file:

```jsonc
"mod_file": "mods/chromatic.mod.json"
```

If both `mods` and `mod_file` are set, `mod_file` is applied first, then inline `mods`.

---

## Groups

Groups let you define a named block of config + mods and reference it from multiple items.
This avoids repeating the same visual preset across many items.

```jsonc
"groups": {
  "hype": {
    "config": {
      "trail_alpha": 0.8,
      "trail_chromatic": 2.0,
      "trail_glow": 0.4,
      "motion_blur_samples": 6
    },
    "mods": [
      { "type": "colorize", "mode": "hue" }
    ]
  },
  "chill": {
    "config": { "trail_alpha": 0.3, "trail_decay_curve": "gaussian" }
  }
}
```

Items reference a group by name:

```jsonc
{ "input": "charts/boss.json", "group": "hype" }
```

**Merge order** (highest wins):
1. Item-level `config` / `mods`
2. Group `config` / `mods`
3. Script `defaults`

Group mods are prepended before item-level inline mods.

---

## Variables

Variables allow you to define values once and reference them as `"$name"` in any string
within the script. This is useful for keeping commonly-tuned values in one place.

```jsonc
"variables": {
  "spd":  1.4,
  "glow": 0.35,
  "bg_d": 80
},

"presets": {
  "vibrant": {
    "trail_glow":   "$glow",
    "chart_speed":  "$spd",
    "bg_dim":       "$bg_d"
  }
}
```

Variable substitution is applied recursively to all string values in `presets`, `defaults`,
`groups`, and per-item `config`.  Non-string values (numbers, booleans) are passed through
unchanged; only strings starting with `$` are substituted.

---

## Presets

Presets are named config templates that items can extend with `"preset": "name"`.

```jsonc
"presets": {
  "vibrant": {
    "trail_alpha":        0.7,
    "trail_decay_curve":  "gaussian",
    "trail_blur_quality": 3,
    "trail_chromatic":    1.5,
    "trail_glow":         0.35,
    "motion_blur_samples": 4,
    "motion_blur_curve":  "gaussian"
  },
  "minimal": {
    "trail_alpha":  0.0,
    "show_hitfx":   false,
    "show_particles": false
  }
}
```

Use in an item's config:

```jsonc
"config": { "preset": "vibrant", "chart_speed": 1.8 }
```

Item-level keys win over preset keys. Preset keys win over `defaults`.

---

## Filter

A `filter` on an item causes the item to be skipped if the filter does not match.
This is most useful when combined with `discover` to selectively exclude charts.

```jsonc
"filter": {
  "levels":       ["AT", "IN"],   // item.level must be in this list (case-insensitive)
  "min_notes":    100,            // skip if total notes < min_notes
  "max_notes":    2000,           // skip if total notes > max_notes
  "name_contains": "CHAOS",       // substring match on item.name (case-insensitive)
  "tags_any":     ["featured"]    // item must have at least one of these tags (OR logic)
}
```

All conditions are AND-combined (all must pass). Empty / omitted fields are ignored.

---

## Global Filter

`global_filter` is applied to **every** item in the playlist after discovery and after the
explicit `items` list is built.  Items that fail the global filter are removed.

```jsonc
"global_filter": {
  "levels": ["AT", "IN"],
  "min_notes": 50
}
```

This is useful when you want to filter everything generated by `discover` without
adding a `filter` to each discovered item individually.

---

## Discover (Auto-Discovery)

Instead of listing items manually, `discover` scans a directory for chart files at runtime.

```jsonc
"discover": {
  "directory": "charts/",        // root directory to scan
  "levels":    ["AT", "IN"],     // only include files whose level matches
  "recursive": true,             // scan subdirectories
  "sort_by":   "name",           // "name" | "notes" | "difficulty" | "random"
  "limit":     50                // cap at 50 items (-1 = no limit)
}
```

Discovered items inherit `defaults` config and any `global_filter`.

`discover_limit` at the top level provides an additional cap across all discovery calls.

### Combining discover and items

If both `discover` and `items` are present, discovered items are appended after the
explicit items list.

---

## Resume State

When `resume_file` is set, the renderer saves the current cursor position (item index)
after each item completes.  On the next run, playback resumes from where it left off.

```jsonc
"resume_file": ".playlist_resume.json"
```

The file contains `{"cursor": N}`.  Delete it to restart from the beginning.

Setting `resume_file` to an empty string (or omitting it) disables resume.

---

## Python Generator — gen_chartscript.py

`scripts/gen_chartscript.py` generates `.chartscript.json` playlists from a chart directory.

```bash
python3 scripts/gen_chartscript.py --charts_dir charts/ --output playlist.chartscript.json
```

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--charts_dir DIR` | `charts/` | Chart root directory to scan |
| `--output FILE` | `playlist.chartscript.json` | Output file |
| `--mode MODE` | `sequence` | `sequence` / `shuffle` / `loop` |
| `--preset PRESET` | `minimal` | Visual preset: `ambient` / `showcase` / `battle` / `minimal` |
| `--levels LEVELS` | all | Comma-separated level filter, e.g. `AT,IN` |
| `--notes_window N` | -1 | Auto-cut segments at N notes per chart |
| `--tail_time T` | 0.5 | Seconds after last note in window |
| `--order ORDER` | `name` | `name` / `notes` / `random` |
| `--seed SEED` | 0 | Random seed (0 = random) |
| `--limit N` | -1 | Max charts to include |
| `--repeat N` | 1 | Playlist repeat count (0 = infinite) |
| `--resume` | off | Enable resume state (writes `.resume.json`) |
| `--compat` | off | Also write legacy `advance.json` |
| `--info` | off | Print playlist stats summary |

### Built-in Presets

| Preset | Description |
|--------|-------------|
| `ambient` | Soft Gaussian trail, light glow, low-intensity |
| `showcase` | High-quality trail, chromatic offset, glow, Gaussian motion blur |
| `battle` | Additive trail blend, heavy chromatic, strong glow, fast motion blur |
| `minimal` | No effects (clean, fastest) |

### Examples

```bash
# Shuffle of all AT charts, showcase visuals, segments of 200 notes each
python3 scripts/gen_chartscript.py \
  --charts_dir charts/ --levels AT \
  --mode shuffle --preset showcase \
  --notes_window 200 --tail_time 1.0 \
  --output at_showcase.chartscript.json

# Infinite loop of all charts, ambient visuals
python3 scripts/gen_chartscript.py \
  --charts_dir charts/ --mode loop --repeat 0 \
  --preset ambient --output ambient_loop.chartscript.json
```

---

## Complete Example

```jsonc
// Full-featured playlist showcasing all v2 features.
// Usage: phigros_render --script full_example.chartscript.json
{
  "version": 2,
  "name": "Showcase Playlist",
  "mode": "shuffle",
  "shuffle_seed": 0,
  "repeat": 0,
  "resume_file": ".resume.json",
  "discover_limit": 30,

  // ── Variables ─────────────────────────────────────────────────────────────
  "variables": {
    "spd":  1.4,
    "glow": 0.35,
    "chro": 1.8
  },

  // ── Presets ───────────────────────────────────────────────────────────────
  "presets": {
    "vibrant": {
      "chart_speed":        "$spd",
      "trail_alpha":        0.72,
      "trail_frames":       10,
      "trail_decay_curve":  "gaussian",
      "trail_blur_quality": 3,
      "trail_chromatic":    "$chro",
      "trail_glow":         "$glow",
      "motion_blur_samples": 4,
      "motion_blur_shutter": 0.55,
      "motion_blur_curve":  "gaussian"
    },
    "boss": {
      "trail_alpha":  0.9,
      "trail_blend":  "add",
      "trail_chromatic": 2.5,
      "trail_glow":   0.6,
      "hitfx_intensity": 1.4,
      "particle_count": 14
    }
  },

  // ── Groups ────────────────────────────────────────────────────────────────
  "groups": {
    "hype": {
      "config": { "preset": "vibrant" },
      "mods":   [{ "type": "colorize", "mode": "hue", "hue_s": 0.9 }]
    },
    "boss_fight": {
      "config": { "preset": "boss" },
      "mods":   [{ "type": "mirror" }, { "type": "speed", "mul": 1.1 }]
    }
  },

  // ── Global filter ─────────────────────────────────────────────────────────
  "global_filter": { "levels": ["AT", "IN"], "min_notes": 80 },

  // ── Defaults ──────────────────────────────────────────────────────────────
  "defaults": {
    "chart_speed": 1.0,
    "trail_alpha": 0.5,
    "trail_frames": 8,
    "trail_decay_curve": "gaussian",
    "show_hitfx": true,
    "show_particles": true
  },

  // ── Transition ────────────────────────────────────────────────────────────
  "transition": { "type": "fade", "duration": 0.4 },

  // ── Auto-discovery ────────────────────────────────────────────────────────
  "discover": {
    "directory": "charts/",
    "recursive": true,
    "sort_by": "random"
  },

  // ── Explicit items (appended before discovered items) ─────────────────────
  "items": [
    // Tutorial-style entry: play first 100 notes, repeat until you pass
    {
      "input": "charts/tutorial/EZ.json",
      "name": "Tutorial",
      "level": "EZ",
      "tags": ["intro"],
      "notes_window": 100,
      "tail_time": 1.5,
      "config": { "chart_speed": 0.9 },
      "on_complete": {
        "action":      "next",
        "min_score":   700000,
        "else_action": "repeat"
      }
    },

    // Multi-segment item: play two highlights of the same chart
    {
      "input": "charts/epic_song/AT.json",
      "name": "Epic Song (highlights)",
      "level": "AT",
      "group": "hype",
      "weight": 3,
      "tags": ["featured", "fast"],
      "segments": [
        { "start": 0.0,  "end": 20.0 },
        { "start": 75.0, "end": 95.0 }
      ],
      "on_complete": { "action": "next" }
    },

    // Boss chart: gated unlock — only advance if score >= 950000
    {
      "input": "charts/final_boss/IN.json",
      "name": "Final Boss",
      "level": "IN",
      "group": "boss_fight",
      "weight": 1,
      "tags": ["boss"],
      "on_complete": {
        "action":      "next",
        "min_score":   950000,
        "else_action": "repeat"
      }
    }
  ]
}
```
