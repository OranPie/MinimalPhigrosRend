"""
Font texture caching system for ModernGL backend.

Renders text to PIL images, converts to GPU textures, and caches
with LRU eviction policy (max 128 entries).
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from ..texture import texture_from_pil_image


class FontCache:
    """
    Font texture cache with LRU eviction.

    Uses PIL to render text to images, then uploads to GPU textures.
    Maintains a cache of up to 128 unique text strings to avoid
    repeated texture uploads.
    """

    def __init__(self, ctx: Any, max_cache_size: int = 128):
        """
        Initialize font cache.

        Args:
            ctx: ModernGL context
            max_cache_size: Maximum number of cached text textures
        """
        self.ctx = ctx
        self.max_cache_size = max_cache_size

        # LRU cache implementation
        self._text_cache: Dict[Tuple[str, int, str], Any] = {}
        self._text_cache_order: List[Tuple[str, int, str]] = []

    def get_text_texture(self, text: str, *, font_path: Optional[str] = None, font_size: int = 16):
        """
        Get or create a texture for the given text.

        Args:
            text: Text to render
            font_path: Path to TrueType font file (optional)
            font_size: Font size in pixels

        Returns:
            GPU texture containing rendered text, or None if text is empty
        """
        if not text:
            return None

        # Check cache
        fp = str(font_path or "")
        key = (fp, int(font_size), str(text))
        cached = self._text_cache.get(key)
        if cached is not None:
            return cached

        # Render text to PIL image
        img = self._render_text_to_image(text, font_path=font_path, font_size=font_size)

        # Convert to GPU texture
        tex = texture_from_pil_image(self.ctx, img, flip_y=True)

        # Add to cache with LRU eviction
        self._add_to_cache(key, tex)

        return tex

    def _render_text_to_image(self, text: str, *, font_path: Optional[str], font_size: int):
        """
        Render text to a PIL image.

        Args:
            text: Text to render
            font_path: Path to TrueType font file (optional)
            font_size: Font size in pixels

        Returns:
            PIL Image with rendered text
        """
        try:
            from PIL import Image, ImageDraw, ImageFont  # type: ignore
        except:
            raise SystemExit(
                "ModernGL text rendering requires Pillow. Install it: pip install pillow"
            )

        # Load font
        font = self._load_font(font_path, font_size)

        # Split text into lines
        parts = str(text).split("\n")

        # Calculate line height
        try:
            bb = font.getbbox("Hg")
            line_h = max(1, int(bb[3] - bb[1]))
        except:
            line_h = max(1, int(font_size))

        # Calculate width for each line
        widths: List[int] = []
        for s in parts:
            if not s:
                widths.append(0)
                continue
            try:
                b = font.getbbox(s)
                widths.append(max(1, int(b[2] - b[0])))
            except:
                widths.append(max(1, int(len(s) * max(6, int(font_size * 0.6)))))

        # Calculate image dimensions
        w = max(1, max(widths) if widths else 1)
        h = max(1, int(line_h) * max(1, len(parts)))

        # Create image and draw text
        img = Image.new("RGBA", (int(w), int(h)), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        y = 0
        for s in parts:
            if s:
                draw.text((0, y), s, font=font, fill=(255, 255, 255, 255))
            y += int(line_h)

        return img

    def _load_font(self, font_path: Optional[str], font_size: int):
        """
        Load font from path or use default.

        Args:
            font_path: Path to TrueType font file (optional)
            font_size: Font size in pixels

        Returns:
            PIL ImageFont object
        """
        try:
            from PIL import ImageFont  # type: ignore
        except:
            raise SystemExit(
                "ModernGL text rendering requires Pillow. Install it: pip install pillow"
            )

        fp = str(font_path or "")
        use_path = None

        # Try provided path
        if fp and fp and __import__("os").path.exists(fp):
            use_path = fp
        # Fallback to cmdysj.ttf if available
        elif __import__("os").path.exists("cmdysj.ttf"):
            use_path = "cmdysj.ttf"

        # Load font
        try:
            if use_path:
                font = ImageFont.truetype(use_path, int(font_size))
            else:
                font = ImageFont.load_default()
        except:
            font = ImageFont.load_default()

        return font

    def _add_to_cache(self, key: Tuple[str, int, str], tex: Any):
        """
        Add texture to cache with LRU eviction.

        Args:
            key: Cache key (font_path, font_size, text)
            tex: GPU texture to cache
        """
        self._text_cache[key] = tex
        self._text_cache_order.append(key)

        # Evict oldest entry if cache is full
        if len(self._text_cache_order) > self.max_cache_size:
            oldk = self._text_cache_order.pop(0)
            old = self._text_cache.pop(oldk, None)
            if old is not None:
                try:
                    old.release()
                except:
                    pass

    def clear(self):
        """Clear the entire cache and release all textures."""
        for tex in self._text_cache.values():
            try:
                tex.release()
            except:
                pass
        self._text_cache.clear()
        self._text_cache_order.clear()

    def get_stats(self) -> Dict[str, int]:
        """
        Get cache statistics.

        Returns:
            Dictionary with cache_size and max_cache_size
        """
        return {
            'cache_size': len(self._text_cache),
            'max_cache_size': self.max_cache_size,
        }
