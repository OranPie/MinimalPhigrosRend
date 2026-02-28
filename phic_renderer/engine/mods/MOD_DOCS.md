# PhicRenderer Mod System Documentation

The mod system enables comprehensive transformation and modification of chart notes and judgment lines. Mods are configured via JSON/YAML format and applied during chart initialization.

## Table of Contents

- [Overview](#overview)
- [Configuration Format](#configuration-format)
- [Filter System](#filter-system)
- [Available Mods](#available-mods)
  - [Timing Mods](#timing-mods)
    - [Transpose](#transpose)
    - [Stretch](#stretch)
    - [Reverse](#reverse)
    - [Quantize](#quantize)
  - [Spatial Mods](#spatial-mods)
    - [Mirror](#mirror)
    - [Scale](#scale)
    - [Wave](#wave)
  - [Variation Mods](#variation-mods)
    - [Randomize](#randomize)
    - [Fade](#fade)
  - [Generation Mods](#generation-mods)
    - [Thin Out](#thin-out)
    - [Stutter](#stutter)
    - [Compress/Zip](#compresszip)
    - [Attach](#attach)
  - [Visual Mods](#visual-mods)
    - [Colorize](#colorize)
    - [Visual Effects](#visual-effects)
  - [Conversion Mods](#conversion-mods)
    - [Full Blue](#full-blue)
    - [Hold Convert](#hold-convert)
  - [Rule-Based Mods](#rule-based-mods)
    - [Note Rules](#note-rules)
    - [Line Rules](#line-rules)
- [Mod Execution Order](#mod-execution-order)
- [Advanced Examples](#advanced-examples)

## Overview

Mods are applied sequentially during chart initialization. Each mod can optionally include a **filter** to selectively target specific notes based on various criteria.

**Core Concepts:**
- **Filters** determine which notes/lines are affected by a mod
- **Mods** transform note properties (position, timing, size, appearance, etc.)
- **Execution Order** is significant - mods are processed sequentially, with each mod potentially affecting subsequent ones

## Configuration Format

Configure mods within the `mods` section of your chart configuration:

```json
{
  "mods": {
    "mirror": {
      "enable": true,
      "axis": "x"
    },
    "scale": {
      "enable": true,
      "size": 1.5
    }
  }
}
```

## Filter System

Most mods support a `filter` parameter for selective application. Filters support multiple criteria that are combined with AND logic:

### Basic Filters

```json
"filter": {
  "kinds": [1, 2],          // Note types: 1=tap, 2=drag, 3=hold, 4=flick
  "not_kinds": [3],         // Exclude these note types
  "line_ids": [0, 1],       // Only notes on these judgment lines
  "above": true,            // Only notes above (true) or below (false) the line
  "fake": false             // Only real (false) or fake (true) notes
}
```

### Time Range Filters

```json
"filter": {
  "time_min": 0.0,          // Minimum hit time (seconds)
  "time_max": 10.0,         // Maximum hit time (seconds)
  "t_end_min": 5.0,         // Minimum end time (for holds)
  "t_end_max": 15.0         // Maximum end time (for holds)
}
```

### Position Range Filters

```json
"filter": {
  "x_min": -200,            // Minimum x position (px)
  "x_max": 200,             // Maximum x position (px)
  "y_min": -50,             // Minimum y offset (px)
  "y_max": 50               // Maximum y offset (px)
}
```

### Property Range Filters

```json
"filter": {
  "speed_min": 0.5,         // Minimum speed multiplier
  "speed_max": 2.0,         // Maximum speed multiplier
  "size_min": 50,           // Minimum size (px)
  "size_max": 200,          // Maximum size (px)
  "nid_min": 0,             // Minimum note ID
  "nid_max": 100            // Maximum note ID
}
```

### Pattern Filters

```json
"filter": {
  "every": 2,               // Every Nth note (e.g., 2 = every other note)
  "offset": 0,              // Offset for modulo pattern
  "probability": 0.5        // Random selection (0-1, deterministic based on note ID)
}
```

### Filter Examples

```json
// Select only tap notes in the first 10 seconds
"filter": {
  "kinds": [1],
  "time_max": 10.0
}

// Select only notes on the left side
"filter": {
  "x_max": 0
}

// Select every 3rd note starting from note 1
"filter": {
  "every": 3,
  "offset": 1
}

// Randomly select 30% of all drag notes (deterministic)
"filter": {
  "kinds": [2],
  "probability": 0.3
}
```

## Available Mods

### Timing Mods

#### Transpose

Shift note timing forward or backward by a constant offset.

**Config aliases:** `transpose`, `time_shift`, `shift`, `delay`

```json
"transpose": {
  "enable": true,
  "offset": -0.5,          // Time offset in seconds (negative = earlier, positive = later)
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `offset` (float): Time offset in seconds (default: 0.0)
- `filter` (object): Optional filter

**Use Cases:**
- Adjust chart timing globally
- Create delayed echo sections
- Fix timing desync issues

**Example:** Delay all flick notes by 0.1 seconds:
```json
"transpose": {
  "enable": true,
  "offset": 0.1,
  "filter": {"kinds": [4]}
}
```

---

#### Stretch

Stretch or compress chart timing by a multiplicative factor.

**Config aliases:** `stretch`, `time_stretch`, `tempo`, `speed_change`

```json
"stretch": {
  "enable": true,
  "factor": 1.5,           // Time multiplier (1.0 = no change, 2.0 = half speed, 0.5 = double speed)
  "anchor": 0.0,           // Anchor point in seconds (timing is stretched relative to this point)
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `factor` (float): Time multiplier (default: 1.0)
- `anchor` (float): Anchor point in seconds (default: 0.0)
- `filter` (object): Optional filter

**Use Cases:**
- Create slow-motion or fast-forward sections
- Adjust BPM changes
- Create time dilation effects

**Example:** Make the chart twice as fast:
```json
"stretch": {
  "enable": true,
  "factor": 0.5,
  "anchor": 0.0
}
```

---

#### Reverse

Reverse note order in time, mirroring the timeline.

**Config aliases:** `reverse`, `backwards`, `invert_time`

```json
"reverse": {
  "enable": true,
  "anchor": null,          // Anchor point (default: midpoint of all notes)
  "preserve_holds": true,  // Keep hold durations correct (swap t_hit and t_end)
  "filter": {
    "kinds": [1, 2, 4]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `anchor` (float): Anchor point in seconds (default: automatic midpoint)
- `preserve_holds` (bool): Preserve hold note durations (default: true)
- `filter` (object): Optional filter

**Use Cases:**
- Create backwards charts
- Generate mirrored sections
- Design palindrome patterns

**Example:** Reverse all notes:
```json
"reverse": {
  "enable": true
}
```

---

#### Quantize

Snap note properties to grid/steps for precise alignment.

**Config aliases:** `quantize`, `snap`, `grid`

```json
"quantize": {
  "enable": true,
  "time_grid": 0.125,      // Snap time to multiples (e.g., 0.125 = 16th note at 120 BPM)
  "x_grid": 50,            // Snap x position to multiples (px)
  "y_grid": 10,            // Snap y offset to multiples (px)
  "size_grid": 5,          // Snap size to multiples (px)
  "filter": {
    "kinds": [1, 2, 4]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `time_grid` (float): Time quantization step (seconds) (optional)
- `x_grid` (float): X position quantization step (px) (optional)
- `y_grid` (float): Y offset quantization step (px) (optional)
- `size_grid` (float): Size quantization step (px) (optional)
- `filter` (object): Optional filter

**Use Cases:**
- Align notes to rhythmic grid
- Clean up imprecise charting
- Create geometric patterns

**Example:** Snap all notes to 8th note grid (at 120 BPM):
```json
"quantize": {
  "enable": true,
  "time_grid": 0.25
}
```

---

### Spatial Mods

#### Mirror

Flip notes horizontally or vertically around a center point.

**Config aliases:** `mirror`, `flip`, `reflect`

```json
"mirror": {
  "enable": true,
  "axis": "x",             // "x" (horizontal) or "y" (vertical)
  "center": 0,             // Center point for mirroring
  "flip_side": true,       // Also flip above/below side
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `axis` (string): Mirror axis - `"x"` (horizontal), `"y"` (vertical) (default: "x")
- `center` (float): Center point for mirroring (default: 0)
- `flip_side` (bool): Also flip above/below side (default: true for x-axis)
- `filter` (object): Optional filter

**Use Cases:**
- Create symmetrical patterns
- Generate mirrored doubles
- Design balanced layouts

**Example:** Create left-right symmetry:
```json
"mirror": {
  "enable": true,
  "axis": "x",
  "center": 0,
  "flip_side": true
}
```

---

#### Scale

Scale note sizes, speeds, or positions multiplicatively.

**Config aliases:** `scale`, `zoom`, `resize`

```json
"scale": {
  "enable": true,
  "size": 1.5,             // Size multiplier (150% larger)
  "speed": 1.2,            // Speed multiplier (20% faster)
  "x": 1.5,                // X position multiplier (spread out horizontally)
  "y": 1.0,                // Y position multiplier
  "x_center": 0,           // Center point for X scaling
  "y_center": 0,           // Center point for Y scaling
  "filter": {
    "kinds": [1, 2, 4]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `size` (float): Size multiplier (default: 1.0)
- `speed` (float): Speed multiplier (default: 1.0)
- `x` (float): X position multiplier from center (default: 1.0)
- `y` (float): Y position multiplier from center (default: 1.0)
- `x_center` (float): Center point for X scaling (default: 0)
- `y_center` (float): Center point for Y scaling (default: 0)
- `filter` (object): Optional filter

**Use Cases:**
- Adjust note visibility
- Create zoom effects
- Modify note density

**Example:** Make all notes 50% larger and more spread out:
```json
"scale": {
  "enable": true,
  "size": 1.5,
  "x": 1.3
}
```

---

#### Wave

Create sinusoidal wave patterns in note positioning or properties.

**Config aliases:** `wave`, `sine`, `oscillate`

```json
"wave": {
  "enable": true,
  "mode": "time",          // "time" (based on hit time) or "index" (based on note order)
  "axis": "x",             // "x", "y", "size", "alpha", "speed"
  "amplitude": 100,        // Wave amplitude (units depend on axis)
  "frequency": 1.0,        // Wave frequency (cycles per second for time mode)
  "phase": 0.0,            // Phase offset (0-1, as fraction of cycle)
  "offset": 0.0,           // DC offset added to wave
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `mode` (string): Wave mode - `"time"`, `"index"` (default: "time")
- `axis` (string): Target axis - `"x"`, `"y"`, `"size"`, `"alpha"`, `"speed"` (default: "x")
- `amplitude` (float): Wave amplitude (default: 100)
- `frequency` (float): Wave frequency (default: 1.0)
- `phase` (float): Phase offset 0-1 (default: 0.0)
- `offset` (float): DC offset (default: 0.0)
- `filter` (object): Optional filter

**Use Cases:**
- Create flowing serpentine patterns
- Generate rhythmic size pulsations
- Design organic movement

**Example:** Create horizontal wave pattern:
```json
"wave": {
  "enable": true,
  "mode": "time",
  "axis": "x",
  "amplitude": 150,
  "frequency": 0.5
}
```

---

### Variation Mods

#### Randomize

Add controlled random variation to note properties.

**Config aliases:** `randomize`, `random`, `chaos`, `jitter`

```json
"randomize": {
  "enable": true,
  "seed": 12345,           // Optional: deterministic randomness
  "x_range": [-50, 50],    // Random x offset range
  "y_range": [-20, 20],    // Random y offset range
  "time_range": [-0.05, 0.05],  // Random time offset range
  "speed_range": [0.8, 1.2],    // Random speed multiplier range
  "size_range": [0.9, 1.1],     // Random size multiplier range
  "alpha_range": [0.8, 1.0],    // Random alpha range
  "flip_side_chance": 0.1,      // Probability to flip above/below
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `seed` (int): Random seed for reproducibility (optional)
- `x_range` (array): X offset range [min, max] (px) (default: [0, 0])
- `y_range` (array): Y offset range [min, max] (px) (default: [0, 0])
- `time_range` (array): Time offset range [min, max] (seconds) (default: [0, 0])
- `speed_range` (array): Speed multiplier range [min, max] (default: [1, 1])
- `size_range` (array): Size multiplier range [min, max] (default: [1, 1])
- `alpha_range` (array): Alpha range [min, max] (default: [1, 1])
- `flip_side_chance` (float): Probability to flip side (0-1) (default: 0)
- `filter` (object): Optional filter

**Use Cases:**
- Add organic variation
- Create chaotic sections
- Increase difficulty through uncertainty

**Example:** Add moderate chaos with consistent results:
```json
"randomize": {
  "enable": true,
  "seed": 42,
  "x_range": [-30, 30],
  "time_range": [-0.03, 0.03],
  "flip_side_chance": 0.05
}
```

---

#### Fade

Fade notes in/out based on time progression.

**Config aliases:** `fade`, `alpha`, `opacity`

```json
"fade": {
  "enable": true,
  "mode": "time",          // "time" or "constant"
  "time_start": 0.0,       // Fade in from this time
  "time_end": 10.0,        // Fade out to this time
  "alpha_start": 0.0,      // Alpha at start time
  "alpha_end": 1.0,        // Alpha at end time
  "alpha_min": 0.1,        // Minimum alpha clamp
  "alpha_max": 1.0,        // Maximum alpha clamp
  "constant_alpha": 0.5,   // Alpha for "constant" mode
  "filter": {
    "kinds": [1, 2, 4]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `mode` (string): Fade mode - `"time"`, `"constant"` (default: "time")
- `time_start` (float): Start time for fade (optional)
- `time_end` (float): End time for fade (optional)
- `alpha_start` (float): Alpha at start (default: 0.0)
- `alpha_end` (float): Alpha at end (default: 1.0)
- `alpha_min` (float): Minimum alpha (default: 0.0)
- `alpha_max` (float): Maximum alpha (default: 1.0)
- `constant_alpha` (float): Constant alpha (default: 0.5)
- `filter` (object): Optional filter

**Use Cases:**
- Create fade-in intros
- Gradually hide notes
- Design visibility challenges

**Example:** Fade in over first 5 seconds, fade out after 30:
```json
"fade": {
  "enable": true,
  "mode": "time",
  "time_start": 0.0,
  "time_end": 30.0,
  "alpha_start": 0.0,
  "alpha_end": 0.3
}
```

---

### Generation Mods

#### Thin Out

Remove notes by pattern or probability to reduce density.

**Config aliases:** `thin_out`, `thin`, `remove`, `reduce`

```json
"thin_out": {
  "enable": true,
  "mode": "every",         // "every", "random", "keep"
  "every": 2,              // Keep every Nth note
  "offset": 0,             // Starting offset for "every" mode
  "probability": 0.3,      // Removal probability for "random" mode
  "keep_count": 100,       // Keep only first N notes for "keep" mode
  "seed": 12345,           // Random seed for "random" mode
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `mode` (string): Removal mode - `"every"`, `"random"`, `"keep"` (default: "every")
- `every` (int): Keep every Nth note (default: 2)
- `offset` (int): Starting offset (default: 0)
- `probability` (float): Removal probability 0-1 (default: 0.3)
- `keep_count` (int): Number of notes to keep (default: 100)
- `seed` (int): Random seed (optional)
- `filter` (object): Optional filter

**Use Cases:**
- Create easier chart versions
- Generate rhythmic gaps
- Reduce visual clutter

**Example:** Remove every other tap note:
```json
"thin_out": {
  "enable": true,
  "mode": "every",
  "every": 2,
  "filter": {"kinds": [1]}
}
```

---

#### Stutter

Create stutter/echo effects by repeating notes with decay.

**Config aliases:** `stutter`, `echo`, `repeat`

```json
"stutter": {
  "enable": true,
  "count": 3,              // Number of repetitions (including original)
  "delay": 0.05,           // Time delay between repetitions (seconds)
  "x_offset": 20,          // X position offset per repetition
  "y_offset": 0,           // Y position offset per repetition
  "alpha_decay": 0.8,      // Alpha multiplier per repetition
  "size_decay": 0.9,       // Size multiplier per repetition
  "filter": {
    "kinds": [1, 4]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `count` (int): Number of repetitions (default: 3)
- `delay` (float): Time delay between repetitions (seconds) (default: 0.05)
- `x_offset` (float): X offset per repetition (px) (default: 20)
- `y_offset` (float): Y offset per repetition (px) (default: 0)
- `alpha_decay` (float): Alpha multiplier per repetition (default: 0.8)
- `size_decay` (float): Size multiplier per repetition (default: 0.9)
- `filter` (object): Optional filter

**Use Cases:**
- Create rhythmic stutter effects
- Generate echo patterns
- Design ghost note sequences

**Example:** Create 5 echoes of each flick note:
```json
"stutter": {
  "enable": true,
  "count": 5,
  "delay": 0.03,
  "alpha_decay": 0.7,
  "filter": {"kinds": [4]}
}
```

---

#### Compress/Zip

Duplicate each note N times at the same position.

**Config aliases:** `compress_zip`, `compress`, `zip`, `duplicate`

```json
"compress_zip": {
  "enable": true,
  "count": 20,             // Number of duplicates per note
  "filter": {
    "kinds": [1, 2]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `count` (int): Number of duplicates per note (default: 2)
- `filter` (object): Optional filter

**Use Cases:**
- Create multi-hit notes
- Increase score multipliers
- Generate dense patterns

**Example:** Make each tap note become 10 identical notes:
```json
"compress_zip": {
  "enable": true,
  "count": 10,
  "filter": {"kinds": [1]}
}
```

---

#### Attach

Add offset notes to each existing note.

**Config aliases:** `attach`, `attach_note`, `add_note`

```json
"attach": {
  "enable": true,
  "kind": 4,               // Type of attached note (1-4)
  "x_offset": 100,         // Horizontal offset (px)
  "y_offset": 0,           // Vertical offset (px)
  "time_offset": 0.0,      // Time offset (seconds)
  "above": null,           // Side of attached note (null/true/false/"flip")
  "filter": {
    "kinds": [1]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `kind` (int): Attached note type (1=tap, 2=drag, 3=hold, 4=flick) (default: 4)
- `x_offset` (float): Horizontal offset (px) (default: 100)
- `y_offset` (float): Vertical offset (px) (default: 0)
- `time_offset` (float): Time offset (seconds) (default: 0.0)
- `above` (mixed): Side - null/true/false/"flip" (default: null)
- `filter` (object): Optional filter

**Use Cases:**
- Create chord patterns
- Add companion notes
- Generate offset rhythms

**Example:** Add a flick 100px right of each tap:
```json
"attach": {
  "enable": true,
  "kind": 4,
  "x_offset": 100,
  "filter": {"kinds": [1]}
}
```

---

### Visual Mods

#### Colorize

Apply color tints to notes with various modes.

**Config aliases:** `colorize`, `tint`, `color`, `paint`

```json
"colorize": {
  "enable": true,
  "mode": "gradient",      // "constant", "gradient", "by_kind", "by_line"
  "tint": "#FF00FF",       // Note tint (constant mode)
  "tint_hitfx": "#FF00FF", // Hit effect tint
  "gradient_start": "#FF0000",  // Gradient start color
  "gradient_end": "#0000FF",    // Gradient end color
  "by_kind": {             // Color mapping by note type
    "1": "#FF0000",        // Tap = red
    "2": "#00FF00",        // Drag = green
    "3": "#0000FF",        // Hold = blue
    "4": "#FFFF00"         // Flick = yellow
  },
  "by_line": {             // Color mapping by line ID
    "0": "#FF0000",
    "1": "#00FF00"
  },
  "filter": {
    "kinds": [1, 2, 4]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `mode` (string): Color mode - `"constant"`, `"gradient"`, `"by_kind"`, `"by_line"` (default: "constant")
- `tint` (string/array): Note tint color (hex or RGB array)
- `tint_hitfx` (string/array): Hit effect tint color
- `gradient_start` (string/array): Gradient start color (gradient mode)
- `gradient_end` (string/array): Gradient end color (gradient mode)
- `by_kind` (object): Color mapping by note kind (by_kind mode)
- `by_line` (object): Color mapping by line ID (by_line mode)
- `filter` (object): Optional filter

**Use Cases:**
- Create visual themes
- Color-code note types
- Design gradient effects

**Example:** Create rainbow gradient over time:
```json
"colorize": {
  "enable": true,
  "mode": "gradient",
  "gradient_start": "#FF0000",
  "gradient_end": "#0000FF"
}
```

---

#### Visual Effects

Visual-only effects that don't modify note/line data. See existing visual mod documentation for details.

**Config aliases:** `visual`, `ui`, `display`

---

### Conversion Mods

#### Full Blue

Convert all notes to blue (above the line).

**Config aliases:** `full_blue`, `all_blue`, `blue_mode`

```json
"full_blue": {
  "enable": true
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)

**Use Cases:**
- Simplify color patterns
- Create monochrome charts
- Reduce visual complexity

---

#### Hold Convert

Convert between note types (holds to taps/drags or vice versa).

**Config aliases:** `hold_convert`, `hold_to_tap`, `tap_to_hold`

```json
"hold_convert": {
  "enable": true,
  "mode": "hold_to_tap",   // Conversion mode
  "filter": {
    "kinds": [3]
  }
}
```

**Options:**
- `enable` (bool): Enable this mod (default: true)
- `mode` (string): Conversion mode (e.g., "hold_to_tap", "hold_to_drag", "tap_to_hold")
- `filter` (object): Optional filter

**Use Cases:**
- Simplify hold patterns
- Create alternative patterns
- Adjust difficulty

---

### Rule-Based Mods

#### Note Rules

Apply conditional modifications based on filters.

**Config aliases:** `note_rules`, `notes`

```json
"note_rules": {
  "rules": [
    {
      "filter": {
        "kinds": [1],
        "time_max": 10.0
      },
      "set": {
        "kind": 2,
        "speed": 0.5,
        "alpha": 0.8,
        "tint": "#FF0000"
      }
    }
  ]
}
```

**Options:**
- `rules` (array): List of rule objects with `filter` and `set` properties

**Set properties:** kind, speed, alpha, size, side/above, x/y offsets, tint, tint_hitfx, fake

**Use Cases:**
- Complex conditional transformations
- Multi-criteria modifications
- Fine-grained control

---

#### Line Rules

Apply conditional modifications to judgment lines.

**Config aliases:** `line_rules`, `lines`

```json
"line_rules": {
  "rules": [
    {
      "filter": {
        "line_ids": [0, 1]
      },
      "set": {
        "color": "#FF0000",
        "force_alpha": 0.5
      }
    }
  ]
}
```

**Options:**
- `rules` (array): List of rule objects with `filter` and `set` properties

**Set properties:** color, name, force_alpha

**Use Cases:**
- Style judgment lines
- Create visual themes
- Adjust line visibility

---

## Mod Execution Order

Mods are applied in this specific sequence:

1. **visual** - Visual-only effects
2. **full_blue** - Convert all notes to blue
3. **hold_convert** - Convert note types
4. **transpose** - Shift timing
5. **stretch** - Stretch/compress timing
6. **reverse** - Reverse note order
7. **quantize** - Snap to grid
8. **mirror** - Flip spatially
9. **scale** - Scale properties
10. **wave** - Apply wave patterns
11. **randomize** - Add random variation
12. **fade** - Modify alpha
13. **thin_out** - Remove notes
14. **stutter** - Create echoes
15. **compress_zip** - Duplicate notes
16. **attach** - Add offset notes
17. **colorize** - Apply colors
18. **note_rules** - Conditional modifications
19. **line_rules** - Line modifications

**Why order matters:**
- Timing mods (transpose, stretch, reverse) apply early to establish temporal structure
- Quantize before randomize ensures notes align before adding chaos
- Note generation (stutter, compress_zip, attach) applies before colorization
- Rules apply last for final adjustments

---

## Advanced Examples

### Example 1: Chaotic Mirror with Gradient

```json
{
  "mods": {
    "mirror": {
      "enable": true,
      "axis": "x",
      "flip_side": true
    },
    "randomize": {
      "enable": true,
      "seed": 42,
      "x_range": [-20, 20],
      "time_range": [-0.02, 0.02]
    },
    "colorize": {
      "enable": true,
      "mode": "gradient",
      "gradient_start": "#FF0000",
      "gradient_end": "#00FFFF"
    }
  }
}
```

### Example 2: Stuttered Echo Hell

```json
{
  "mods": {
    "stutter": {
      "enable": true,
      "count": 4,
      "delay": 0.04,
      "x_offset": 30,
      "alpha_decay": 0.75,
      "filter": {"kinds": [1, 4]}
    },
    "attach": {
      "enable": true,
      "kind": 4,
      "x_offset": 100,
      "filter": {"kinds": [1]}
    },
    "compress_zip": {
      "enable": true,
      "count": 5
    }
  }
}
```

### Example 3: Wave-Based Organic Pattern

```json
{
  "mods": {
    "wave": {
      "enable": true,
      "mode": "time",
      "axis": "x",
      "amplitude": 150,
      "frequency": 0.75,
      "phase": 0.0
    },
    "wave": {
      "enable": true,
      "mode": "time",
      "axis": "size",
      "amplitude": 50,
      "frequency": 1.2,
      "phase": 0.25
    },
    "colorize": {
      "enable": true,
      "mode": "gradient",
      "gradient_start": "#00FF00",
      "gradient_end": "#0000FF"
    }
  }
}
```

### Example 4: Reverse Stretched Chart

```json
{
  "mods": {
    "reverse": {
      "enable": true
    },
    "stretch": {
      "enable": true,
      "factor": 1.3,
      "anchor": 0.0
    },
    "colorize": {
      "enable": true,
      "mode": "by_kind",
      "by_kind": {
        "1": "#FF0000",
        "2": "#00FF00",
        "3": "#0000FF",
        "4": "#FFFF00"
      }
    }
  }
}
```

### Example 5: Ultra-Complex Transformation

```json
{
  "mods": {
    "transpose": {
      "enable": true,
      "offset": 0.5
    },
    "quantize": {
      "enable": true,
      "time_grid": 0.125,
      "x_grid": 50
    },
    "mirror": {
      "enable": true,
      "axis": "x"
    },
    "scale": {
      "enable": true,
      "size": 1.4,
      "x": 1.3
    },
    "wave": {
      "enable": true,
      "axis": "y",
      "amplitude": 80,
      "frequency": 1.0
    },
    "randomize": {
      "enable": true,
      "seed": 12345,
      "x_range": [-25, 25],
      "time_range": [-0.04, 0.04],
      "flip_side_chance": 0.1
    },
    "fade": {
      "enable": true,
      "mode": "time",
      "time_start": 0.0,
      "time_end": 25.0,
      "alpha_start": 0.3,
      "alpha_end": 1.0
    },
    "stutter": {
      "enable": true,
      "count": 3,
      "delay": 0.05,
      "alpha_decay": 0.8,
      "filter": {
        "kinds": [1],
        "probability": 0.3
      }
    },
    "attach": {
      "enable": true,
      "kind": 4,
      "x_offset": 80,
      "filter": {
        "kinds": [1],
        "time_max": 15.0
      }
    },
    "colorize": {
      "enable": true,
      "mode": "gradient",
      "gradient_start": "#FF00FF",
      "gradient_end": "#00FFFF"
    }
  }
}
```

---

## Advanced Tips

### Combining Filters

All filter criteria use AND logic - all conditions must be satisfied:

```json
"filter": {
  "kinds": [1],           // Must be tap
  "time_max": 10.0,       // AND before 10s
  "x_min": -100,          // AND on left side
  "above": true           // AND above line
}
```

### Deterministic Randomness

Use `seed` for consistent reproducible results:

```json
"randomize": {
  "seed": 42,  // Same seed always produces same results
  "x_range": [-50, 50]
}
```

### Probability-Based Selection

Use `probability` for random sampling with deterministic behavior:

```json
"filter": {
  "probability": 0.5  // Select 50% of notes (deterministic based on note ID)
}
```

### Pattern Generation

Use `every` + `offset` for repeating patterns:

```json
// Pattern: keep, keep, remove, keep, keep, remove, ...
"thin_out": {
  "mode": "every",
  "every": 3,
  "offset": 2
}
```

### Effect Layering

Combine multiple mods for complex results:

```json
{
  "mirror": {"enable": true},          // 1. Mirror
  "scale": {"size": 1.5},              // 2. Scale up
  "wave": {"axis": "y"},               // 3. Add vertical wave
  "randomize": {"x_range": [-10, 10]}, // 4. Add slight chaos
  "fade": {"constant_alpha": 0.8},     // 5. Make semi-transparent
  "colorize": {"mode": "gradient"}     // 6. Apply gradient
}
```

---

## Troubleshooting

**Q: My mod isn't working!**
- Verify `enable: true` is set
- Check filter criteria - they might be too restrictive
- Review mod execution order - earlier mods affect later ones
- Test with simpler configurations first

**Q: Notes are disappearing!**
- Check `thin_out` mod settings and filters
- Ensure filters aren't excluding all notes
- Verify `fake: false` in filter if needed

**Q: Timing is incorrect after applying mods!**
- Timing mods (transpose, stretch, reverse, quantize, randomize) modify hit times
- Notes are automatically re-sorted by hit time after timing mods
- Check `time_range` in randomize for unintended jitter

**Q: Colors aren't applying!**
- Ensure colorize mod is enabled
- Check if mode matches your configuration
- Verify color format (hex "#FF0000" or RGB [255, 0, 0])

**Q: How do I disable a mod temporarily?**
- Set `enable: false` in the mod configuration
- Or remove the mod block entirely

**Q: Can I create custom mods?**
- Yes! Add a new Python file in `phic_renderer/runtime/mods/`
- Follow the pattern of existing mods (take one parameter: `mods_cfg, notes, lines`)
- Import and register in `__init__.py`
- Update `apply_mods()` function with appropriate execution order

**Q: How do I debug filter matching?**
- Test with empty filter first to affect all notes
- Add one filter criterion at a time
- Use simpler mods (like fade with constant mode) to visualize which notes are affected

---

## Reference Tables

### Note Types

| Value | Type | Description |
|-------|------|-------------|
| 1 | Tap | Blue/yellow circle |
| 2 | Drag | Yellow line segment |
| 3 | Hold | Long press with head/tail |
| 4 | Flick | Vertical swipe |

### Color Formats

Colors can be specified as:
- Hex string: `"#FF0000"` (red)
- RGB array: `[255, 0, 0]` (red)
- RGB tuple: `(255, 0, 0)` (red)

### Alpha Values

Alpha can be specified as:
- Float 0-1: `0.5` (50% opacity)
- Integer 0-255: `128` (50% opacity)

### Common BPM Time Grids

At 120 BPM:
- Whole note: `2.0`
- Half note: `1.0`
- Quarter note: `0.5`
- 8th note: `0.25`
- 16th note: `0.125`
- 32nd note: `0.0625`

---

**Version:** 2.0
**Last Updated:** 2025-12-23
**Total Mods:** 19
