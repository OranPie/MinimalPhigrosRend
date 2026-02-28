# PhicRenderer Module Structure

Complete guide to the PhicRenderer codebase organization.

## Directory Tree

```
phic_renderer/
├── __init__.py                 # Package entry point
├── __main__.py                 # CLI entry point
├── app.py                      # Application entry (CLI argument parsing)
├── types.py                    # Core data types (RuntimeNote, RuntimeLine, NoteState)
├── state.py                    # [DEPRECATED] Global state (use RenderConfig instead)
├── compat.py                   # Compatibility layer for migration
│
├── config/                     # Configuration Management
│   ├── __init__.py
│   ├── schema.py              # RenderConfig dataclass (immutable configuration)
│   ├── loader.py              # Config v2 loading (JSONC format)
│   └── defaults.py            # Default configuration values
│
├── core/                       # Core Abstractions
│   ├── __init__.py
│   ├── session.py             # GameSession base class (ABC)
│   ├── context.py             # ResourceContext (session-scoped resources)
│   ├── types.py               # [Link to ../types.py]
│   ├── constants.py           # NOTE_TYPE_COLORS, etc.
│   ├── fx.py                  # Hit effect management
│   └── ui.py                  # [MOVED to ui/] Legacy location
│
├── engine/                     # Game Logic (renamed from runtime/)
│   ├── __init__.py
│   ├── hold_system.py         # Unified hold note system (3 files → 1 class)
│   ├── note_manager.py        # Note lifecycle and visibility management
│   ├── advance.py             # Playlist/advance mode
│   ├── effects.py             # HitFX, ParticleBurst data structures
│   ├── judge.py               # Judge class (timing windows, scoring)
│   ├── judge_script.py        # Judgment script parsing
│   ├── kinematics.py          # Note position, line state evaluation
│   ├── visibility.py          # Visibility culling
│   ├── timewarp.py            # Time warping effects
│   ├── compat.py              # Backward-compatible wrappers
│   ├── SINGLETON_MIGRATION.md # Migration guide for global singletons
│   └── mods/                  # Game modifiers
│       ├── __init__.py
│       ├── base.py            # Base mod functionality
│       ├── visual.py          # Visual modifications
│       ├── full_blue.py       # Full blue mod
│       ├── hold_convert.py    # Hold-to-tap/drag conversion
│       └── rules.py           # Note/line rule application
│
├── backends/                   # Rendering Backends (renamed from renderer/)
│   ├── __init__.py            # Backend dispatcher
│   ├── ARCHITECTURE.md        # Backend architecture documentation
│   │
│   ├── pygame/                # Pygame Backend (software rendering)
│   │   ├── __init__.py        # Backend entry point
│   │   ├── session.py         # PygameSession class (main coordinator)
│   │   ├── game_loop.py       # [STUB] Game loop (future extraction)
│   │   │
│   │   │── Rendering Helpers (from old renderer/pygame/)
│   │   ├── frame_renderer.py  # Frame assembly and rendering
│   │   ├── hold.py            # Hold note rendering (3-slice technique)
│   │   ├── hold_logic.py      # Hold state management
│   │   ├── hold_cache.py      # Hold rendering cache (LRU)
│   │   ├── pointer_input.py   # Input handling (PointerManager)
│   │   ├── manual_judgement.py # Manual judgment mode
│   │   ├── simulateplay.py    # Simulated gameplay
│   │   │
│   │   │── UI
│   │   ├── curses_ui.py       # Curses-based terminal UI
│   │   ├── textual_ui.py      # Textual-based modern terminal UI
│   │   ├── ui_rendering.py    # UI overlay rendering
│   │   ├── post_ui.py         # Post-render UI elements
│   │   │
│   │   │── Effects
│   │   ├── hitfx.py           # Hit effect rendering
│   │   ├── hitsound.py        # Hit sound playback
│   │   ├── particles.py       # Particle effect rendering
│   │   ├── trail_effect.py    # Trail visual effects
│   │   ├── motion_blur.py     # Motion blur effect
│   │   │
│   │   │── Resources
│   │   ├── respack_loader.py  # Resource pack loading
│   │   ├── background.py      # Background image handling
│   │   ├── fonts.py           # Font loading
│   │   │
│   │   │── Performance
│   │   ├── surface_pool.py    # Surface pooling for memory efficiency
│   │   ├── transform_cache.py # Transform caching
│   │   ├── texture_atlas.py   # Texture atlas management
│   │   ├── batch_renderer.py  # Batched rendering
│   │   │
│   │   │── Utilities
│   │   ├── draw.py            # Low-level drawing primitives
│   │   ├── init_helpers.py    # Initialization helpers
│   │   ├── rendering_helpers.py # Rendering utilities
│   │   ├── judge_helpers.py   # Judgment utilities
│   │   ├── miss_logic.py      # Miss detection
│   │   ├── recording_utils.py # Recording progress display
│   │   ├── record_writer.py   # Frame writing
│   │   ├── debug_judge_windows.py # Debug visualization
│   │   └── debug_pointer.py   # Pointer debugging
│   │
│   └── moderngl/              # ModernGL Backend (hardware-accelerated)
│       ├── __init__.py        # Backend entry point
│       ├── backend.py         # Main ModernGL rendering (was moderngl_backend.py)
│       ├── app.py             # ModernGL application
│       ├── renderer2d.py      # 2D renderer
│       ├── sprite.py          # Sprite rendering
│       ├── texture.py         # Texture management
│       ├── context.py         # OpenGL context
│       ├── loop.py            # Game loop
│       └── respack_loader.py  # Resource pack loading
│
├── chart/                      # Chart Format Parsers (renamed from formats/)
│   ├── __init__.py
│   ├── loader.py              # Chart loading facade
│   ├── official.py            # Official Phigros format (merged wrapper+impl)
│   ├── rpe.py                 # RPE (Re:PhiEdit) format (merged wrapper+impl)
│   └── pec.py                 # PEC format
│
├── assets/                     # Asset Loading (renamed from io/)
│   ├── __init__.py
│   ├── loader.py              # Chart loader (from chart_loader_impl.py)
│   ├── chartpack.py           # Chart pack handling (from chart_pack_impl.py)
│   ├── respack.py             # Resource pack loading (from respack_impl.py)
│   ├── background.py          # Background loading
│   └── fonts.py               # Font loading
│
├── recording/                  # Video Recording System
│   ├── __init__.py
│   ├── base.py                # Recording base classes
│   ├── frame_recorder.py      # Frame recording
│   ├── video_recorder.py      # Video encoding (ffmpeg)
│   ├── audio_mixer.py         # Audio mixing for recordings
│   └── presets.py             # Encoding presets
│
├── audio/                      # Audio System
│   ├── __init__.py
│   ├── base.py                # Audio backend interface
│   └── backends/
│       ├── pygame_audio.py    # Pygame audio backend
│       └── openal_audio.py    # OpenAL audio backend
│
├── ui/                         # UI Utilities (backend-agnostic)
│   ├── __init__.py
│   ├── scoring.py             # Score calculation (compute_score, progress_ratio)
│   ├── formatting.py          # Title formatting (format_title)
│   └── i18n.py                # Internationalization
│
├── math/                       # Math Utilities
│   ├── __init__.py
│   ├── easing.py              # Easing functions
│   ├── tracks.py              # Track interpolation
│   └── util.py                # Math utility functions
│
├── utils/                      # General Utilities
│   ├── __init__.py
│   ├── colors.py              # Color utilities (from runtime/render.py)
│   └── logging_setup.py       # Logging configuration
│
├── api/                        # Public API
│   ├── __init__.py
│   ├── playlist.py            # Playlist/sequence management API
│   └── README.md              # API documentation
│
├── [LEGACY - Deprecated]      # Old structure (backward compatibility)
├── runtime/                    # [USE engine/ instead]
├── formats/                    # [USE chart/ instead]
├── io/                         # [USE assets/ instead]
└── renderer/                   # [USE backends/ instead]
```

## Module Descriptions

### Configuration (`config/`)

**Purpose**: Immutable configuration management

- **schema.py**: `RenderConfig` dataclass with all rendering settings
  - Visual: expand_factor, note_scale, flow_speed
  - Line: force_line_alpha01
  - Effects: trail, motion_blur
  - Methods: `from_state_module()`, `to_state_module()` for compatibility

- **loader.py**: Load configuration from JSONC files
- **defaults.py**: Default configuration values

**Key Classes**:
- `RenderConfig`: Frozen dataclass with all rendering parameters

### Core (`core/`)

**Purpose**: Core abstractions and base classes

- **session.py**: `GameSession` abstract base class
  - Defines lifecycle: `initialize()`, `run_game_loop()`, `cleanup()`
  - Inherited by `PygameSession`, `ModernGLSession`

- **context.py**: `ResourceContext` for session-scoped resources
  - Owns: respack, fonts, surface_pool, transform_cache, etc.
  - Method: `cleanup()` for proper resource release

- **types.py**: Core data structures
  - `RuntimeNote`: Note representation
  - `RuntimeLine`: Judgment line representation
  - `NoteState`: Runtime state tracking

- **constants.py**: Constants like `NOTE_TYPE_COLORS`
- **fx.py**: Hit effect management utilities

**Key Classes**:
- `GameSession`: Abstract session base
- `ResourceContext`: Session resource ownership
- `RuntimeNote`, `RuntimeLine`, `NoteState`: Data types

### Engine (`engine/`)

**Purpose**: Game logic (note management, judgment, physics)

- **hold_system.py**: Unified hold note system
  - Consolidates: hold.py, hold_logic.py, hold_cache.py
  - Methods: `maintenance()`, `finalize()`, `tick_effects()`, `draw()`
  - Owns: `HoldCache` for performance

- **note_manager.py**: Note lifecycle management
  - Methods: `update_visibility()`, `get_visible_notes()`, `find_next_note_index()`
  - Handles visibility culling

- **judge.py**: `Judge` class
  - Timing windows (PERFECT/GOOD/BAD)
  - Combo tracking, accuracy calculation

- **kinematics.py**: Physics calculations
  - `eval_line_state()`: Line position/rotation/alpha at time t
  - `note_world_pos()`: Note world position from line state
  - Updated to accept explicit config parameters (no global state)

- **visibility.py**: Visibility culling
  - `precompute_t_enter()`: Precompute when notes enter screen
  - `_note_visible_on_screen()`: Check if note is visible
  - Updated to accept explicit config parameters

- **advance.py**: Playlist/advance mode (24KB)
- **judge_script.py**: Judgment script parsing
- **effects.py**: HitFX, ParticleBurst data structures
- **timewarp.py**: Time warping effects

**Mods** (`engine/mods/`):
- **base.py**: Base mod functionality
- **visual.py**: Visual modifications
- **full_blue.py**: Full blue mod
- **hold_convert.py**: Hold-to-tap/drag conversion
- **rules.py**: Note/line rule application

**Key Classes**:
- `HoldSystem`: Unified hold management
- `NoteManager`: Note lifecycle
- `Judge`: Judgment system

### Backends (`backends/`)

**Purpose**: Rendering backend implementations

- **__init__.py**: Backend dispatcher
  - Routes to pygame or moderngl based on args

- **pygame/session.py**: `PygameSession` class
  - Extends `GameSession`
  - Currently wraps old `pygame_backend.py::run()` for compatibility
  - Future: Will use modular `GameLoop`, `InputHandler`, etc.

- **pygame/**: 30+ helper modules for rendering
  - **frame_renderer.py**: Main rendering orchestration
  - **hold.py, hold_logic.py, hold_cache.py**: Hold note system
  - **pointer_input.py**: Input handling
  - **curses_ui.py, textual_ui.py**: Terminal UIs for headless mode
  - **surface_pool.py, transform_cache.py**: Performance optimizations

- **moderngl/**: Hardware-accelerated rendering
  - **backend.py**: Main ModernGL implementation
  - **renderer2d.py**: 2D rendering engine
  - OpenGL-based for better performance

**Key Classes**:
- `PygameSession`: Pygame session coordinator
- `GameLoop`: [Future] Extracted game loop

### Chart (`chart/`)

**Purpose**: Chart format parsing

- **official.py**: Official Phigros format parser
- **rpe.py**: RPE (Re:PhiEdit) format parser
- **pec.py**: PEC format parser

Each parser outputs standardized `RuntimeLine` and `RuntimeNote` structures.

### Assets (`assets/`)

**Purpose**: Asset loading (charts, resources, media)

- **loader.py**: Chart loading
- **chartpack.py**: Chart pack (.zip/.pez) handling
- **respack.py**: Resource pack loading
- **background.py**: Background image loading
- **fonts.py**: Font loading

### Recording (`recording/`)

**Purpose**: Video recording and audio mixing

- **frame_recorder.py**: Capture frames
- **video_recorder.py**: Encode to video (ffmpeg)
- **audio_mixer.py**: Mix audio tracks
- **presets.py**: Encoding presets

### Audio (`audio/`)

**Purpose**: Audio backend abstraction

- **base.py**: Audio backend interface
- **backends/pygame_audio.py**: Pygame audio implementation
- **backends/openal_audio.py**: OpenAL implementation

### UI (`ui/`)

**Purpose**: Backend-agnostic UI utilities

- **scoring.py**: Score calculation functions
  - `compute_score()`: Calculate final score
  - `progress_ratio()`: Calculate progress percentage

- **formatting.py**: Text formatting
  - `format_title()`: Format chart title and subtitle

- **i18n.py**: Internationalization

### Math (`math/`)

**Purpose**: Math utilities

- **easing.py**: Easing functions
- **tracks.py**: Track interpolation
- **util.py**: General math utilities

### Utils (`utils/`)

**Purpose**: General utilities

- **colors.py**: Color utilities (tint functions)
- **logging_setup.py**: Logging configuration

### API (`api/`)

**Purpose**: Public scripting API

- **playlist.py**: Playlist/sequence management API (1,174 lines)

## Import Patterns

### New Code (Recommended)

```python
from phic_renderer.config import RenderConfig
from phic_renderer.core import GameSession, ResourceContext
from phic_renderer.engine import HoldSystem, NoteManager
from phic_renderer.backends import run
from phic_renderer.backends.pygame.session import PygameSession
```

### Legacy Code (Backward Compatible)

```python
from phic_renderer import state  # Deprecated, issues warning
from phic_renderer.runtime.judge import Judge  # Works, points to engine.judge
from phic_renderer.renderer import run  # Works, points to backends.run
```

## Module Dependency Graph

```
                  ┌──────────┐
                  │  config  │ (No dependencies)
                  └─────┬────┘
                        │
         ┌──────────────┼──────────────┐
         │              │              │
    ┌────▼────┐   ┌────▼────┐   ┌────▼────┐
    │  core   │   │  chart  │   │ assets  │
    └────┬────┘   └─────────┘   └─────────┘
         │
    ┌────▼────┐
    │ engine  │
    └────┬────┘
         │
    ┌────▼────┐
    │backends │ (pygame, moderngl)
    └─────────┘
```

**Key Principle**: No circular dependencies. Lower layers don't depend on upper layers.

## File Size Reference

- **Large files** (>1000 lines):
  - `api/playlist.py`: 1,174 lines
  - `backends/pygame/` (old pygame_backend.py): 2,440 lines (to be refactored)
  - `backends/moderngl/app.py`: 1,428 lines

- **Medium files** (500-1000 lines):
  - `backends/pygame/frame_renderer.py`: 633 lines
  - `chart/rpe.py`: 613 lines

- **Well-sized files** (<500 lines): Most modules

## Testing Strategy

- **Unit tests**: Test individual modules (HoldSystem, NoteManager)
- **Integration tests**: Test full rendering pipeline
- **Regression tests**: Compare output with reference
- **Backward compatibility tests**: Verify old imports work

## Future Work

1. **Complete GameLoop extraction**: Pull loop from `pygame_backend.py`
2. **Create InputHandler**: Wrap `PointerManager`
3. **Create JudgeEngine**: Wrap judgment orchestration
4. **Remove legacy files**: Clean up `runtime/`, `formats/`, `io/`
5. **Migrate call sites**: Update to new APIs

---

**Last Updated**: After comprehensive refactoring (Phases 1-5)
**Status**: Production-ready, fully documented
