"""Session-Based Architecture Documentation

# Phase 4: Session Refactor - New Architecture

## Overview

Phase 4 introduces a session-based architecture that replaces the monolithic
`pygame_backend.py::run()` function (2,440 lines) with modular components.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Entry                        │
│                    (app.py / record.py)                      │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
          ┌──────────────────────────────┐
          │    backends/__init__.py       │
          │    Backend Dispatcher         │
          └──────────┬───────────────────┘
                     │
          ┌──────────┴──────────┐
          ▼                     ▼
  ┌───────────────┐     ┌──────────────────┐
  │ PygameSession │     │ ModernGLSession  │
  │   (Phase 4)   │     │   (Future)       │
  └───────┬───────┘     └──────────────────┘
          │
          ├─────────────────────────────────────────┐
          │                                         │
          ▼                                         ▼
  ┌────────────────┐                     ┌──────────────────┐
  │   GameLoop     │                     │  Subsystems      │
  │   (Future)     │                     │                  │
  │                │                     │ - NoteManager    │
  │ - Frame timing │                     │ - HoldSystem     │
  │ - Update cycle │                     │ - JudgeEngine    │
  │ - Render cycle │                     │ - InputHandler   │
  └────────────────┘                     └──────────────────┘
```

## Component Breakdown

### 1. PygameSession (Core Coordinator)

**File**: `backends/pygame/session.py`
**Status**: ✅ Implemented (Phase 4)

- Extends `GameSession` base class
- Manages session lifecycle (initialize, run, cleanup)
- Owns ResourceContext with session-scoped resources
- Currently wraps existing `pygame_backend.py::run()` for compatibility
- Future: Will delegate to GameLoop and subsystems

**Key Methods**:
- `initialize()` - Set up Pygame, create screen, initialize resources
- `run_game_loop()` - Run the main loop (currently delegates to old code)
- `cleanup()` - Clean up resources

### 2. GameLoop (Future Extraction)

**File**: `backends/pygame/game_loop.py`
**Status**: 🚧 Stub created

Will extract:
- Main loop from `pygame_backend.py::run()`
- Frame timing (clock.tick)
- Update/render cycle
- Event processing
- Recording frame capture
- Stop conditions

### 3. Subsystems (Already Exist)

- **NoteManager**: `engine/note_manager.py` (Phase 2)
- **HoldSystem**: `engine/hold_system.py` (Phase 2)
- **JudgeEngine**: To be created (wraps runtime/judge.py)
- **InputHandler**: To be created (wraps pygame/pointer_input.py)

## Current State (Phase 4)

### What's Implemented

✅ `backends/__init__.py` - Backend dispatcher
✅ `backends/pygame/__init__.py` - Pygame backend entry
✅ `backends/pygame/session.py` - PygameSession class
✅ Backward compatibility maintained

### How It Works Now

```python
# Entry point (backends/__init__.py)
def run(args, **ctx):
    backend = args.backend or "pygame"
    if backend == "pygame":
        from .pygame import run
        return run(args, **ctx)

# Pygame backend (backends/pygame/__init__.py)
from ...renderer.pygame_backend import run  # Re-export existing

# Future: Will use PygameSession
# from .session import PygameSession
# session = PygameSession(config, resources, lines, notes, chart_info)
# return session.run()
```

### Migration Path

**Step 1** (Current): Create structure, maintain compatibility
- ✅ Create `backends/` hierarchy
- ✅ Implement `PygameSession` wrapper
- ✅ Keep existing `pygame_backend.py` functional

**Step 2** (Future): Gradual extraction
- Extract GameLoop from `pygame_backend.py::run()`
- Create InputHandler (from PointerManager)
- Create JudgeEngine (from judge orchestration)
- Update `PygameSession.run_game_loop()` to use new components

**Step 3** (Phase 5): Cleanup
- Remove old `renderer/pygame_backend.py`
- Update all entry points to use `backends/`
- Remove compatibility shims

## Benefits

1. **Modularity**: Clear separation of concerns
2. **Testability**: Can test components in isolation
3. **Maintainability**: Smaller, focused files instead of 2,440-line monolith
4. **Extensibility**: Easy to add new backends (ModernGL, Vulkan, etc.)
5. **Resource Management**: Proper cleanup via session lifecycle

## File Organization

```
backends/
├── __init__.py              # Backend dispatcher
├── pygame/
│   ├── __init__.py          # Pygame backend entry
│   ├── session.py           # PygameSession class (✅)
│   ├── game_loop.py         # GameLoop (🚧 stub)
│   ├── input_handler.py     # Input (future)
│   ├── judge_engine.py      # Judgment (future)
│   ├── frame_builder.py     # Frame assembly (future)
│   │
│   ├── rendering/           # Rendering subsystem
│   │   ├── notes.py
│   │   ├── holds.py
│   │   ├── lines.py
│   │   └── ...
│   │
│   ├── ui/                  # UI rendering
│   │   ├── overlay.py
│   │   ├── post_render.py
│   │   └── debug.py
│   │
│   └── headless/            # Headless/recording UI
│       ├── curses_ui.py
│       └── textual_ui.py
│
└── moderngl/                # Future: ModernGL backend
    └── ...
```

## Next Steps

**Immediate (Phase 4 completion)**:
- ✅ Create session structure
- ⏳ Document architecture
- ⏳ Test compatibility

**Future Iterations**:
- Extract GameLoop
- Create InputHandler
- Create JudgeEngine
- Refactor frame rendering
- Move pygame/ helper modules to backends/pygame/

**Phase 5 (Cleanup)**:
- Remove old `renderer/pygame_backend.py`
- Remove old `runtime/`, `formats/`, `io/` directories
- Update all import paths
- Complete documentation
"""

__all__ = []
