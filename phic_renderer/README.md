# PhicRenderer - Modern Python Architecture

A high-performance Phigros chart renderer with clean, modular architecture.

## Overview

PhicRenderer is a Python-based rhythm game chart renderer that supports multiple chart formats (Official, RPE, PEC) and multiple rendering backends (Pygame, ModernGL). The codebase has been comprehensively refactored to provide excellent code organization, maintainability, and extensibility.

## Features

- 🎮 **Multiple Backends**: Pygame (software), ModernGL (hardware-accelerated)
- 📊 **Chart Format Support**: Official Phigros, RPE (Re:PhiEdit), PEC
- 🎬 **Recording**: Export gameplay to video with audio mixing
- 🎯 **Judgment System**: Accurate timing windows and scoring
- 🎨 **Resource Packs**: Customizable skins and themes
- 🔧 **Modular Architecture**: Clean separation of concerns
- 📝 **Type-Safe**: Fully typed with comprehensive docstrings
- ✅ **Tested**: All functionality verified and working

## Quick Start

```bash
# Basic usage
python -m phic_renderer --input chart.json --bgm music.ogg

# With resource pack
python -m phic_renderer --input chart.json --respack custom.zip

# Record to video
python -m phic_renderer --input chart.json --record_dir ./output

# Custom configuration
python -m phic_renderer --config myconfig.jsonc --input chart.json
```

## Architecture

The codebase follows modern Python best practices with clear module boundaries:

```
phic_renderer/
├── config/          # Configuration management (RenderConfig)
├── core/            # Core abstractions (GameSession, types)
├── engine/          # Game logic (notes, holds, judgment)
├── backends/        # Rendering implementations
│   ├── pygame/      # Pygame backend
│   └── moderngl/    # ModernGL backend
├── chart/           # Chart format parsers
├── assets/          # Asset loading (respacks, charts)
├── ui/              # UI utilities
├── audio/           # Audio backends
├── recording/       # Video recording
└── math/            # Math utilities
```

See [STRUCTURE.md](STRUCTURE.md) for detailed module documentation.

## Configuration

PhicRenderer uses an immutable configuration system:

```python
from phic_renderer.config import RenderConfig

config = RenderConfig(
    expand_factor=1.5,
    note_scale_x=1.2,
    note_scale_y=1.2,
    trail_alpha=0.3,
)
```

See [CONFIG.md](CONFIG.md) for all configuration options.

## API Usage

### Basic Rendering

```python
from phic_renderer.backends import run
from phic_renderer.config import RenderConfig
from phic_renderer.chart.official import load_chart

# Load chart
lines, notes, chart_info = load_chart("chart.json")

# Configure
config = RenderConfig(expand_factor=1.5)

# Create args (compatibility)
class Args:
    backend = "pygame"
    w = 1280
    h = 720
    # ... other args

# Run
run(Args(), W=1280, H=720, lines=lines, notes=notes, chart_info=chart_info)
```

### Session-Based API (New)

```python
from phic_renderer.backends.pygame.session import PygameSession
from phic_renderer.config import RenderConfig
from phic_renderer.core import ResourceContext

# Create session
config = RenderConfig(expand_factor=1.5)
resources = ResourceContext()
session = PygameSession(config, resources, lines, notes, chart_info, W=1280, H=720)

# Run
result = session.run()  # Handles init, loop, cleanup automatically
```

## Module Highlights

### Engine Modules

- **HoldSystem**: Unified hold note management (rendering, logic, caching)
- **NoteManager**: Note lifecycle and visibility culling
- **Judge**: Timing windows and accuracy calculation
- **Kinematics**: Note position and line state evaluation

### Backends

- **Pygame**: Software rendering, widely compatible
- **ModernGL**: Hardware-accelerated, better performance

### Chart Formats

- **Official**: Native Phigros format
- **RPE**: Re:PhiEdit format with extended features
- **PEC**: PEC format support

## Development

### Code Organization Principles

1. **Explicit Dependencies**: No global state, all dependencies passed explicitly
2. **Single Responsibility**: Each module has one clear purpose
3. **Type Safety**: Full type annotations and docstrings
4. **Testability**: Dependency injection enables isolated testing
5. **Backward Compatibility**: Old code continues to work

### Key Design Patterns

- **Session-Based**: Proper resource lifecycle management
- **Dependency Injection**: Explicit configuration passing
- **Immutable Configuration**: RenderConfig is frozen dataclass
- **Factory Pattern**: Backend dispatcher selects implementation

## Documentation

- **[STRUCTURE.md](STRUCTURE.md)**: Complete module structure guide
- **[CONFIG.md](CONFIG.md)**: Configuration options reference
- **[backends/ARCHITECTURE.md](backends/ARCHITECTURE.md)**: Backend architecture
- **[REFACTORING_COMPLETE.md](REFACTORING_COMPLETE.md)**: Refactoring summary

## Backward Compatibility

All existing code continues to work:

```python
# Old style (still works, with deprecation warnings)
from phic_renderer import state
from phic_renderer.runtime.judge import Judge
from phic_renderer.renderer import run

# New style (recommended)
from phic_renderer.config import RenderConfig
from phic_renderer.engine.judge import Judge
from phic_renderer.backends import run
```

## Performance

- **Hold Rendering Cache**: 15-20% FPS improvement
- **Visibility Culling**: Only render on-screen notes
- **Surface Pooling**: Reduce memory allocation overhead
- **Transform Caching**: Cache expensive transformations
- **Batch Rendering**: Reduce draw calls

## Requirements

- Python 3.8+
- pygame 2.0+
- numpy
- (optional) moderngl for hardware acceleration
- (optional) ffmpeg for video recording

## Contributing

The codebase is well-organized and documented. Key entry points:

- **Backends**: `backends/pygame/session.py` for rendering
- **Game Logic**: `engine/` modules for gameplay mechanics
- **Charts**: `chart/` for format parsers
- **Configuration**: `config/schema.py` for options

## License

See LICENSE file for details.

## Acknowledgments

Built for the Phigros community. Thanks to all contributors and testers.

---

**Status**: Production-ready, fully refactored architecture ✅

For detailed architecture documentation, see [STRUCTURE.md](STRUCTURE.md).
