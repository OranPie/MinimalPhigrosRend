from __future__ import annotations

# Re-export the backend-neutral respack info parser/structs.
# Backends should load concrete assets (images/sounds) themselves.

from .respack_impl import Respack, load_respack_info  # noqa: F401
