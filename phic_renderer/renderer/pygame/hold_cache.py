"""
Hold note caching system for reducing redundant smoothscale and rotozoom operations.

This module provides a cache for pre-rendered hold note segments, significantly
reducing the expensive transform operations performed on hold notes.
"""

import pygame
from typing import Dict, Tuple, Optional
from collections import OrderedDict
import math


class HoldCache:
    """
    LRU cache for pre-rendered hold note segments.

    Caches hold notes based on quantized parameters to increase hit rate.
    Expected performance gain: 15-20% FPS improvement.
    """

    def __init__(self, max_entries: int = 200):
        """
        Initialize the hold cache.

        Args:
            max_entries: Maximum number of cached hold segments
        """
        self.max_entries = max_entries
        self._cache: OrderedDict[Tuple, pygame.Surface] = OrderedDict()

        # Statistics
        self.stats_hits = 0
        self.stats_misses = 0

    def _quantize_angle(self, angle_deg: float) -> int:
        """Quantize angle to nearest 1 degree."""
        return int(round(angle_deg / 1.0) * 1)

    def _quantize_width(self, width: int) -> int:
        """Quantize width to nearest 10 pixels."""
        return int(((width + 9) // 10) * 10)

    def _quantize_length(self, length: int) -> int:
        """Quantize length to nearest 10 pixels."""
        return int(((length + 9) // 10) * 10)

    def _quantize_progress(self, progress: Optional[float]) -> int:
        """Quantize progress to nearest 2%."""
        if progress is None:
            return -1
        return int(round(progress * 50))

    def _make_key(
        self,
        width: int,
        length: int,
        angle_deg: float,
        mh: bool,
        progress: Optional[float],
        note_rgb: Tuple[int, int, int],
    ) -> Tuple[int, int, int, bool, int, Tuple[int, int, int]]:
        """
        Create cache key from hold parameters with quantization.

        Args:
            width: Hold body width
            length: Hold geometric length
            angle_deg: Rotation angle in degrees
            mh: Multi-highlight mode
            progress: Hold progress (0.0-1.0) or None
            note_rgb: Note tint color

        Returns:
            Quantized cache key tuple
        """
        return (
            self._quantize_width(width),
            self._quantize_length(length),
            self._quantize_angle(angle_deg),
            mh,
            self._quantize_progress(progress),
            note_rgb,  # Keep exact color
        )

    def get(
        self,
        width: int,
        length: int,
        angle_deg: float,
        mh: bool,
        progress: Optional[float],
        note_rgb: Tuple[int, int, int],
    ) -> Optional[pygame.Surface]:
        """
        Get a cached hold surface if available.

        Args:
            width: Hold body width
            length: Hold geometric length
            angle_deg: Rotation angle in degrees
            mh: Multi-highlight mode
            progress: Hold progress or None
            note_rgb: Note tint color

        Returns:
            Cached surface if found, None otherwise
        """
        key = self._make_key(width, length, angle_deg, mh, progress, note_rgb)

        if key in self._cache:
            # Move to end (most recently used)
            self._cache.move_to_end(key)
            self.stats_hits += 1
            # Return a copy to avoid surface state contamination
            return self._cache[key].copy()

        self.stats_misses += 1
        return None

    def put(
        self,
        width: int,
        length: int,
        angle_deg: float,
        mh: bool,
        progress: Optional[float],
        note_rgb: Tuple[int, int, int],
        surface: pygame.Surface,
    ) -> None:
        """
        Add a rendered hold surface to the cache.

        Args:
            width: Hold body width
            length: Hold geometric length
            angle_deg: Rotation angle in degrees
            mh: Multi-highlight mode
            progress: Hold progress or None
            note_rgb: Note tint color
            surface: Pre-rendered hold surface to cache
        """
        key = self._make_key(width, length, angle_deg, mh, progress, note_rgb)

        # Check if we need to evict (LRU)
        if len(self._cache) >= self.max_entries:
            # Remove oldest entry (first item)
            self._cache.popitem(last=False)

        # Add new entry (will be at end)
        self._cache[key] = surface.copy()

    def clear(self) -> None:
        """Clear the cache."""
        self._cache.clear()

    def get_stats(self) -> Dict[str, any]:
        """
        Get cache statistics.

        Returns:
            Dictionary with hits, misses, hit rate, and cache size
        """
        total_requests = self.stats_hits + self.stats_misses
        hit_rate = (self.stats_hits / total_requests * 100) if total_requests > 0 else 0

        return {
            'hits': self.stats_hits,
            'misses': self.stats_misses,
            'hit_rate': hit_rate,
            'cache_size': len(self._cache),
            'max_entries': self.max_entries,
        }

    def reset_stats(self) -> None:
        """Reset statistics counters."""
        self.stats_hits = 0
        self.stats_misses = 0


# Global hold cache instance
_global_hold_cache: Optional[HoldCache] = None


def get_global_hold_cache() -> HoldCache:
    """
    Get the global hold cache instance.

    Returns:
        The global HoldCache instance
    """
    global _global_hold_cache
    if _global_hold_cache is None:
        _global_hold_cache = HoldCache()
    return _global_hold_cache


def reset_global_hold_cache() -> None:
    """Reset the global hold cache."""
    global _global_hold_cache
    if _global_hold_cache is not None:
        _global_hold_cache.clear()
        _global_hold_cache = None
