# PhicRenderer Configuration Guide

Complete reference for configuring PhicRenderer.

## Overview

PhicRenderer uses an immutable configuration system based on the `RenderConfig` dataclass. Configuration can be provided via:

1. **Python API**: Create `RenderConfig` directly
2. **CLI Arguments**: Pass via command-line flags
3. **Config Files**: Load from JSONC configuration files

## RenderConfig Dataclass

Located in `phic_renderer/config/schema.py`

### Visual Settings

#### expand_factor
- **Type**: `float`
- **Default**: `1.0`
- **Description**: Camera zoom/expand factor. Higher values show more of the playfield.
- **Range**: `0.5` - `3.0` typical
- **Example**: `expand_factor=1.5` shows 1.5x more area

#### note_scale_x
- **Type**: `float`
- **Default**: `1.0`
- **Description**: Horizontal note scaling factor
- **Range**: `0.5` - `2.0` typical
- **Example**: `note_scale_x=1.2` makes notes 20% wider

#### note_scale_y
- **Type**: `float`
- **Default**: `1.0`
- **Description**: Vertical note scaling factor
- **Range**: `0.5` - `2.0` typical
- **Example**: `note_scale_y=0.8` makes notes 20% shorter

#### note_flow_speed_multiplier
- **Type**: `float`
- **Default**: `1.0`
- **Description**: Note approach speed multiplier
- **Range**: `0.1` - `5.0`
- **Example**: `note_flow_speed_multiplier=2.0` makes notes approach twice as fast

#### note_speed_mul_affects_travel
- **Type**: `bool`
- **Default**: `False`
- **Description**: Whether per-note speed_mul affects travel distance (RPE behavior)
- **Example**: `note_speed_mul_affects_travel=True`

### Line Settings

#### force_line_alpha01
- **Type**: `Optional[float]`
- **Default**: `None`
- **Description**: Force all judgment lines to this alpha value (0-1)
- **Range**: `0.0` - `1.0`
- **Example**: `force_line_alpha01=0.5` makes all lines 50% transparent

#### force_line_alpha01_by_lid
- **Type**: `Optional[Dict[int, float]]`
- **Default**: `None`
- **Description**: Force specific line IDs to specific alpha values
- **Example**: `force_line_alpha01_by_lid={0: 1.0, 1: 0.5}` line 0 fully opaque, line 1 at 50%

### Rendering Settings

#### render_overrender
- **Type**: `Optional[float]`
- **Default**: `None` (uses default `2.0`)
- **Description**: Overrender factor for screen bounds
- **Range**: `1.0` - `5.0`
- **Example**: `render_overrender=3.0` renders 3x screen area

### Trail Effect Settings

#### trail_alpha
- **Type**: `Optional[float]`
- **Default**: `None` (disabled)
- **Description**: Trail effect alpha (0-1). Enables motion trails when set.
- **Range**: `0.0` - `1.0`
- **Example**: `trail_alpha=0.3` adds 30% opacity trails

#### trail_frames
- **Type**: `Optional[int]`
- **Default**: `None`
- **Description**: Number of trail frames to keep
- **Range**: `2` - `10`
- **Example**: `trail_frames=5`

#### trail_decay
- **Type**: `Optional[float]`
- **Default**: `None`
- **Description**: Trail fade decay rate
- **Range**: `0.0` - `1.0`
- **Example**: `trail_decay=0.8`

#### trail_blur
- **Type**: `Optional[int]`
- **Default**: `None`
- **Description**: Blur strength for trails (downscale factor)
- **Range**: `0` - `10`
- **Example**: `trail_blur=2`

#### trail_dim
- **Type**: `Optional[int]`
- **Default**: `None`
- **Description**: Trail dimming (dark overlay alpha 0-255)
- **Range**: `0` - `255`
- **Example**: `trail_dim=100`

#### trail_blur_ramp
- **Type**: `Optional[bool]`
- **Default**: `None`
- **Description**: Whether to ramp blur over trail frames
- **Example**: `trail_blur_ramp=True`

#### trail_blend
- **Type**: `Optional[str]`
- **Default**: `None`
- **Description**: Blend mode for trails
- **Options**: `"normal"`, `"add"`, `"multiply"`
- **Example**: `trail_blend="add"`

### Motion Blur Settings

#### motion_blur_samples
- **Type**: `Optional[int]`
- **Default**: `None` (disabled)
- **Description**: Number of motion blur samples. Enables motion blur when set.
- **Range**: `2` - `16`
- **Example**: `motion_blur_samples=8`

#### motion_blur_shutter
- **Type**: `Optional[float]`
- **Default**: `None`
- **Description**: Motion blur shutter angle (0-1)
- **Range**: `0.0` - `1.0`
- **Example**: `motion_blur_shutter=0.5`

## Usage Examples

### Python API

```python
from phic_renderer.config import RenderConfig

# Basic configuration
config = RenderConfig(
    expand_factor=1.5,
    note_scale_x=1.2,
    note_scale_y=1.2,
)

# With trail effects
config = RenderConfig(
    expand_factor=1.5,
    trail_alpha=0.3,
    trail_frames=5,
    trail_blur=2,
)

# With motion blur
config = RenderConfig(
    expand_factor=1.5,
    motion_blur_samples=8,
    motion_blur_shutter=0.5,
)

# Force line transparency
config = RenderConfig(
    force_line_alpha01=0.7,  # All lines 70% opacity
)

# Per-line transparency
config = RenderConfig(
    force_line_alpha01_by_lid={
        0: 1.0,  # Line 0: fully opaque
        1: 0.5,  # Line 1: 50% transparent
        2: 0.3,  # Line 2: 30% transparent
    }
)
```

### CLI Arguments

```bash
# Visual settings
python -m phic_renderer --input chart.json \
    --expand 1.5 \
    --note_scale_x 1.2 \
    --note_scale_y 1.2 \
    --note_flow_speed_multiplier 2.0

# Trail effects
python -m phic_renderer --input chart.json \
    --trail_alpha 0.3 \
    --trail_frames 5 \
    --trail_blur 2 \
    --trail_dim 100

# Motion blur
python -m phic_renderer --input chart.json \
    --motion_blur_samples 8 \
    --motion_blur_shutter 0.5

# Line transparency
python -m phic_renderer --input chart.json \
    --force_line_alpha01 0.7
```

### Config File (JSONC)

Create a `.jsonc` config file:

```jsonc
// myconfig.jsonc
{
  // Visual settings
  "expand_factor": 1.5,
  "note_scale_x": 1.2,
  "note_scale_y": 1.2,
  "note_flow_speed_multiplier": 2.0,

  // Trail effects
  "trail_alpha": 0.3,
  "trail_frames": 5,
  "trail_blur": 2,
  "trail_dim": 100,
  "trail_blend": "add",

  // Motion blur
  "motion_blur_samples": 8,
  "motion_blur_shutter": 0.5,

  // Line settings
  "force_line_alpha01": 0.7,

  // Rendering
  "render_overrender": 3.0
}
```

Use with:
```bash
python -m phic_renderer --config myconfig.jsonc --input chart.json
```

## Compatibility Layer

For backward compatibility with old code using global `state` module:

```python
from phic_renderer import state
from phic_renderer.config import RenderConfig

# Old code (deprecated)
state.expand_factor = 1.5
state.note_scale_x = 1.2

# New code (recommended)
config = RenderConfig(
    expand_factor=1.5,
    note_scale_x=1.2,
)

# Convert between old and new
config = RenderConfig.from_state_module(state)
config.to_state_module(state)  # Update state from config
```

## Helper Functions

### args_to_render_config()

Convert CLI args to RenderConfig:

```python
from phic_renderer.compat import args_to_render_config

# From argparse namespace
config = args_to_render_config(args)

# With overrides
config = args_to_render_config(args, expand=2.0)
```

### create_resource_context()

Create ResourceContext:

```python
from phic_renderer.compat import create_resource_context
from phic_renderer.assets.respack import load_respack

respack = load_respack("custom.zip")
resources = create_resource_context(respack=respack)
```

## Preset Configurations

### Comfortable Play
```python
config = RenderConfig(
    expand_factor=1.3,
    note_scale_x=1.1,
    note_scale_y=1.1,
)
```

### Cinematic Recording
```python
config = RenderConfig(
    expand_factor=1.5,
    trail_alpha=0.4,
    trail_frames=6,
    trail_blur=3,
    motion_blur_samples=12,
    motion_blur_shutter=0.6,
)
```

### High Visibility
```python
config = RenderConfig(
    expand_factor=1.8,
    note_scale_x=1.3,
    note_scale_y=1.3,
    force_line_alpha01=1.0,
)
```

### Minimal/Clean
```python
config = RenderConfig(
    expand_factor=1.2,
    force_line_alpha01=0.3,  # Subtle lines
)
```

## Advanced Usage

### Dynamic Configuration

```python
def create_config_for_difficulty(difficulty: str) -> RenderConfig:
    \"\"\"Create config based on difficulty level.\"\"\"
    if difficulty == "easy":
        return RenderConfig(
            expand_factor=1.8,
            note_scale_x=1.5,
            note_scale_y=1.5,
            note_flow_speed_multiplier=0.8,
        )
    elif difficulty == "hard":
        return RenderConfig(
            expand_factor=1.0,
            note_flow_speed_multiplier=1.5,
        )
    else:
        return RenderConfig()  # Defaults
```

### Per-Chart Configuration

```python
import json
from pathlib import Path

def load_chart_config(chart_path: Path) -> RenderConfig:
    \"\"\"Load chart-specific configuration.\"\"\"
    config_path = chart_path.with_suffix('.config.json')
    if config_path.exists():
        with open(config_path) as f:
            config_dict = json.load(f)
        return RenderConfig(**config_dict)
    return RenderConfig()  # Defaults
```

### Configuration Validation

```python
def validate_config(config: RenderConfig) -> list[str]:
    \"\"\"Validate configuration and return warnings.\"\"\"
    warnings = []

    if config.expand_factor < 0.5:
        warnings.append("expand_factor too small, may clip notes")

    if config.expand_factor > 3.0:
        warnings.append("expand_factor very large, may affect visibility")

    if config.trail_alpha and not config.trail_frames:
        warnings.append("trail_alpha set but trail_frames not set")

    if config.motion_blur_samples and config.motion_blur_samples > 16:
        warnings.append("motion_blur_samples very high, may impact performance")

    return warnings
```

## Performance Considerations

### High Performance
```python
config = RenderConfig(
    # Disable expensive effects
    trail_alpha=None,
    motion_blur_samples=None,
)
```

### Balanced
```python
config = RenderConfig(
    trail_alpha=0.2,
    trail_frames=3,
    motion_blur_samples=4,
)
```

### Quality (Slower)
```python
config = RenderConfig(
    trail_alpha=0.5,
    trail_frames=8,
    trail_blur=4,
    motion_blur_samples=16,
    motion_blur_shutter=0.8,
)
```

## Configuration Best Practices

1. **Use RenderConfig**: Always use the new `RenderConfig` instead of global `state`
2. **Immutable**: Don't try to modify `RenderConfig` after creation (it's frozen)
3. **Explicit**: Pass config explicitly to functions
4. **Validate**: Check configuration makes sense before using
5. **Document**: Add comments explaining unusual configurations
6. **Presets**: Create named presets for common use cases

## Migration from Global State

```python
# OLD (deprecated)
from phic_renderer import state
state.expand_factor = 1.5
state.trail_alpha = 0.3
# ... code that reads state.expand_factor

# NEW (recommended)
from phic_renderer.config import RenderConfig
config = RenderConfig(
    expand_factor=1.5,
    trail_alpha=0.3,
)
# ... pass config explicitly to functions
```

## See Also

- [README.md](README.md) - Project overview
- [STRUCTURE.md](STRUCTURE.md) - Module structure
- [backends/ARCHITECTURE.md](backends/ARCHITECTURE.md) - Backend architecture

---

**Status**: Complete configuration reference
**Last Updated**: After refactoring (Phase 3)
