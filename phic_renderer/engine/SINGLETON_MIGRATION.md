"""Guidelines for converting global singletons to session-scoped resources.

Phase 3 of the refactoring eliminates global state and global singletons.
This document provides guidance for migrating global singleton patterns.

## Global Singletons to Convert

The following global singletons should be converted to session-scoped resources:

### 1. Surface Pool
**Old**: `renderer/pygame/surface_pool.py::get_global_pool()`
**New**: `resources.surface_pool` (set during session initialization)

```python
# Old code
from .surface_pool import get_global_pool
pool = get_global_pool()

# New code
pool = resources.surface_pool  # Passed from ResourceContext
```

### 2. Transform Cache
**Old**: `renderer/pygame/transform_cache.py::get_global_transform_cache()`
**New**: `resources.transform_cache`

```python
# Old code
from .transform_cache import get_global_transform_cache
cache = get_global_transform_cache()

# New code
cache = resources.transform_cache  # Passed from ResourceContext
```

### 3. Texture Atlas
**Old**: `renderer/pygame/texture_atlas.py::get_global_atlas()`
**New**: `resources.texture_atlas`

```python
# Old code
from .texture_atlas import get_global_atlas
atlas = get_global_atlas()

# New code
atlas = resources.texture_atlas  # Passed from ResourceContext
```

### 4. Batch Renderer
**Old**: `renderer/pygame/batch_renderer.py::get_global_batch_renderer()`
**New**: `resources.batch_renderer`

## Migration Pattern

1. **Identify global access**: Find `get_global_*()` calls
2. **Add resource parameter**: Add `resources: ResourceContext` to function signature
3. **Pass resource through**: Update all call sites to pass `resources`
4. **Initialize in session**: Set `resources.X = create_X()` during session init

## Example Migration

```python
# Before (global singleton)
def render_frame(surface, notes):
    pool = get_global_pool()
    temp_surf = pool.get(width, height)
    # ... rendering ...

# After (session-scoped)
def render_frame(surface, notes, resources: ResourceContext):
    temp_surf = resources.surface_pool.get(width, height)
    # ... rendering ...
```

## Timeline

Phase 3 focuses on updating `engine/` module functions to accept explicit parameters.
Phase 4 will update renderer backends to use session-scoped resources.
Phase 5 will remove the global singleton functions entirely.
"""

__all__ = []
