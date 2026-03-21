# Debug Flags Reference

> 🌐 [中文](DEBUG_FLAGS.zh.md)

This page documents every `--debug-flags` token supported by the C++ renderer.

## Usage

Pass one or more flags with `--debug-flags` (or `--debug_flags`).

```bash
./phigros_render chart.json --debug-flags FRAME_TIME|AUDIO_INFO|JUDGE_LINE_NUMBER
```

Rules:

- Separators: `|`, `,`, `+`
- Token normalization: `-`, space, and `.` are accepted and normalized to `_`
- `ALL` enables every debug flag
- Numeric bitmasks are also accepted

## Flags

### Timing / runtime panels

| Flag | Behaviour |
|------|-----------|
| `FRAME_TIME` | Draws a top-left panel with frame time, FPS, chart time, and render resolution. |
| `TIMING_WINDOWS` | Draws a panel listing the Perfect / Good / Bad timing windows in milliseconds. |
| `TIMING_CLOCKS` | Draws a multi-line timing panel with simulation time, audio cursor, drift, render dt, sim dt, sim-steps-per-render, current mode, pause state, and result-screen state. |
| `PERFORMANCE_PROFILER` | Draws a right-side profiler panel with average and max time for each tracked frame phase. |
| `FRAME_TIME_GRAPH` | Draws a small rolling graph of recent frame times. |
| `VISIBILITY_SUMMARY` | Draws a summary panel of visible note counts by kind, multi-hit count, pending/judged/missed counts, and active/holding hold counts. |
| `RECORDING_STATUS` | Draws a recording panel with target FPS, encoder wall FPS, queue usage, capture/output resolution, and frame write timing. When not recording it shows the configured target values. |

### Audio panels

| Flag | Behaviour |
|------|-----------|
| `AUDIO_INFO` | Draws a panel showing whether BGM is loaded/playing, hitsound availability, audio cursor time, whether playback has started, and active pointer count. |
| `AUDIO_WAVEFORM` | Draws a sampled waveform panel from the PCM tap around the current playback time. Shows `PCM tap unavailable` when capture is not available. |
| `AUDIO_SPECTRUM` | Draws a simple bar-spectrum panel derived from recent PCM samples. Shows `PCM tap unavailable` when capture is not available. |

### Judge-line overlays

| Flag | Behaviour |
|------|-----------|
| `JUDGE_LINE_INFO_WINDOW` | Draws a left-side panel listing every visible judge line with position, rotation, alpha, scroll, and scale. |
| `JUDGE_LINE_INFO_ABOVE_LINE` | Draws a small text label above each judge line with line id, alpha, scroll, rotation, and X/Y scale. |
| `JUDGE_LINE_NUMBER` | Draws the judge-line id near each visible line. |
| `LINE_GEOMETRY` | Draws the judge-line segment geometry plus endpoint markers. |
| `LINE_INFO_COLOR_MAPPING` | Uses the judge line’s runtime color when rendering line-related debug text. Without it, line debug text uses neutral colors. |
| `LINE_ALPHA_BAR` | Draws a small alpha bar under each judge line, with fill proportional to runtime line alpha. |
| `SCROLL_SPEED_OVERLAY` | Draws the current scroll value as text beside each judge line. |
| `SPEED_VISUALIZATION` | Draws line motion vectors based on frame-to-frame movement and annotates delta-scroll values. |
| `LINE_ACTIVITY_PANEL` | Draws a right-side “line activity” panel listing the busiest lines, including visible note count, imminent hits, active holds, alpha, and scroll. |

### Note overlays

| Flag | Behaviour |
|------|-----------|
| `NOTE_LINE_NUMBER` | Draws the note id beside each visible note. |
| `NOTE_INFO` | Draws detailed note text near each note: note id, parent line id, kind, hit time, world position, alpha, size, and hold progress for holds. |
| `NOTE_JUDGE_WINDOW` | Draws per-note `dt` text showing time-to-hit in milliseconds and the note kind, color-coded by judgment window proximity. |
| `NOTE_HITBOX` | Draws a box outline around each visible note. For holds it also draws a line from head to tail. |
| `NOTE_TRAIL` | Draws a short historical trail for each visible note using remembered positions from recent frames. |
| `NOTE_APPROACH_GUIDE` | Draws a guide line from each unjudged note to its parent judge line. |
| `NOTE_DENSITY_GRAPH` | Draws a small histogram showing upcoming note density over a fixed forward-looking time window. |
| `VELOCITY_VECTORS` | Draws note motion vectors based on frame-to-frame displacement. |
| `COMBO_ZONES` | Draws note-local boxes for notes currently inside the Bad/Good/Perfect approach windows, color-coded by the active zone. |
| `SIMULTANEOUS_INDICATOR` | Draws a diamond marker on notes flagged as simultaneous / multi-hit (`mh`). |
| `MH_TEXTURE_STATUS` | For visible `mh` notes, draws text showing the note kind and whether the renderer is using the MH texture path (`mh-tex`) or falling back to the base texture (`base-tex`). |

### Input / interaction overlays

| Flag | Behaviour |
|------|-----------|
| `TOUCH_VISUALIZATION` | Draws active input slots as boxes, velocity vectors, slot ids, and peak speeds. |
| `CENTER_CROSSHAIR` | Draws a screen-center crosshair and center dot. |
| `EXPAND_BORDER` | Annotates the original viewport bounds when `expand_factor > 1`, showing the effective expand multiplier. |

### Score / judgment overlays

| Flag | Behaviour |
|------|-----------|
| `SCORE_BREAKDOWN` | Draws a score panel with Perfect/Good/Bad/Miss counts, combo/max combo, judged count, accuracy, and score. |
| `JUDGMENT_HISTORY` | Draws a fading recent-judgment feed, color-coded by result. |
| `MISS_INDICATOR` | Draws a temporary red flash box and `MISS` label on recently missed notes. |

### Hold-specific overlays

| Flag | Behaviour |
|------|-----------|
| `HOLD_STATE` | Draws per-hold progress bars and highlights whether a hold is actively pressed, finalized, or released early. |

### Chart / mode state panels

| Flag | Behaviour |
|------|-----------|
| `CHART_METADATA` | Draws a chart-info panel with line count, playable/fake note counts, per-kind totals, chart offset, and duration. |
| `MIRROR_STATUS` | Draws a small panel showing whether mirror mode is currently on. |

## Notes

- Multiple flags can be combined freely.
- Some overlays depend on runtime state and only appear when relevant. For example:
  - `MH_TEXTURE_STATUS` only labels notes that are already marked `mh`
  - `HOLD_STATE` only draws on visible hold notes
  - `FRAME_TIME_GRAPH` and `NOTE_TRAIL` need a few frames of history before becoming useful
- `LINE_INFO_COLOR_MAPPING` is a modifier flag: it changes the coloring of some line-related debug text rather than drawing a standalone overlay by itself.
