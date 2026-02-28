"""
Background image management for ModernGL backend.

Loads and processes background images, converting them to GPU textures.
Supports optional blur effect for visual consistency with pygame backend.
"""

from __future__ import annotations

from typing import Optional, Tuple, Any


def load_background(ctx: Any, bg_file: Optional[str], W: int, H: int, blur_factor: int = 10) -> Tuple[Optional[Any], Optional[Any]]:
    """
    Load background image and create base + blurred GPU textures.

    Args:
        ctx: ModernGL context
        bg_file: Path to background image file
        W: Target width
        H: Target height
        blur_factor: Blur intensity (higher = more blur)

    Returns:
        Tuple of (base_texture, blurred_texture) or (None, None) if no background
    """
    if not bg_file:
        return None, None

    try:
        from PIL import Image  # type: ignore
    except:
        raise SystemExit(
            "ModernGL background loading requires Pillow. Install it: pip install pillow"
        )

    from ..texture import texture_from_pil_image

    try:
        # Load image
        img = Image.open(str(bg_file))

        # Convert to RGBA
        img = img.convert('RGBA')

        # Resize to target dimensions
        img_scaled = img.resize((W, H), Image.LANCZOS)

        # Create base texture
        bg_base = texture_from_pil_image(ctx, img_scaled, flip_y=True)

        # Create blurred version
        factor = max(1, int(blur_factor))
        small_w = max(1, W // factor)
        small_h = max(1, H // factor)

        # Downsample (creates blur effect)
        img_small = img_scaled.resize((small_w, small_h), Image.LANCZOS)

        # Upsample back to original size (smooths the blur)
        img_blurred = img_small.resize((W, H), Image.LANCZOS)

        # Create blurred texture
        bg_blurred = texture_from_pil_image(ctx, img_blurred, flip_y=True)

        return bg_base, bg_blurred

    except Exception as e:
        # If loading fails, return None
        print(f"Warning: Failed to load background '{bg_file}': {e}")
        return None, None


class BackgroundManager:
    """
    Manager for background textures with caching.

    Caches loaded backgrounds to avoid reloading the same file.
    """

    def __init__(self, ctx: Any):
        """
        Initialize background manager.

        Args:
            ctx: ModernGL context
        """
        self.ctx = ctx
        self._cache: dict[str, Tuple[Optional[Any], Optional[Any]]] = {}

    def load(self, bg_file: Optional[str], W: int, H: int, blur_factor: int = 10) -> Tuple[Optional[Any], Optional[Any]]:
        """
        Load background with caching.

        Args:
            bg_file: Path to background image file
            W: Target width
            H: Target height
            blur_factor: Blur intensity

        Returns:
            Tuple of (base_texture, blurred_texture)
        """
        if not bg_file:
            return None, None

        # Create cache key
        cache_key = f"{bg_file}_{W}_{H}_{blur_factor}"

        # Check cache
        if cache_key in self._cache:
            return self._cache[cache_key]

        # Load and cache
        result = load_background(self.ctx, bg_file, W, H, blur_factor)
        self._cache[cache_key] = result

        return result

    def clear(self):
        """Clear the cache and release all textures."""
        for base_tex, blurred_tex in self._cache.values():
            if base_tex is not None:
                try:
                    base_tex.release()
                except:
                    pass
            if blurred_tex is not None:
                try:
                    blurred_tex.release()
                except:
                    pass
        self._cache.clear()

    def get_stats(self) -> dict[str, int]:
        """
        Get cache statistics.

        Returns:
            Dictionary with cache_size
        """
        return {
            'cache_size': len(self._cache),
        }
