# Kinematics

> 🌐 [中文](KINEMATICS.zh.md)

Kinematics is the layer that turns line state, scroll state, and note-local placement into world-space positions.

## Core Types and Functions

Defined in `engine/kinematics.hpp`:

- `LineState`
- `eval_line_state()`
- `Vec2`
- `note_world_pos()`
- `note_world_pos_cs()`

## `LineState`

A line state bundles the values needed by note evaluation and rendering at time `t`:

- line position: `x`, `y`
- line rotation: `rot`
- alpha values: `alpha01`, `alpha_raw`
- accumulated scroll: `scroll`
- cached trig: `cos_rot`, `sin_rot`

`eval_line_state()` also applies optional forced line-alpha overrides from `RenderConfig`.

## Coordinate Model

The runtime uses a line-local basis:

- tangent direction follows the line rotation
- normal direction is perpendicular to the line
- `x_local_px` moves along the tangent
- scroll distance becomes movement along the normal
- `above` controls the sign of that normal motion

## Note Position Evaluation

`note_world_pos()` and `note_world_pos_cs()` compute note world positions from:

- current line state
- note-local placement
- current scroll value
- target scroll value at note hit or hold end
- flow-speed multiplier and speed-mul behavior flags

`note_world_pos_cs()` is the hot-path form that takes precomputed cosine and sine values and avoids repeated trig calls.

## Hold and Speed Rules

Important runtime rules:

- hold heads can optionally stay pinned at the judge line while holding
- hold tails use `scroll_end`
- non-hold travel can optionally be scaled by per-note `speed_mul`
- hold-tail travel always considers `speed_mul` for the tail path

## Render-Adjacent Controls

Kinematics provides the base positions. Render-time control events then apply additional adjustments such as:

- alpha modulation
- x-position multiplier
- extra y offset
- size scaling
- skew

That separation is intentional: kinematics solves the base geometry, render applies presentation modifiers.

## Related Docs

- [MATH.md](MATH.md)
- [RENDER.md](RENDER.md)
- [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
