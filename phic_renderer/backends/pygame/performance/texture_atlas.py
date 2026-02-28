"""
Texture atlas system for efficient sprite rendering.

Packs multiple textures into a single large surface to reduce
texture switching overhead and enable batch rendering.
"""

import pygame
from typing import Dict, List, Tuple, Optional
import math


class TextureAtlas:
    """
    Texture atlas that packs multiple sprites into a single surface.

    Pre-generates sprites at multiple scales for runtime efficiency.
    Expected performance gain: 20-25% FPS improvement when combined with batch renderer.
    """

    def __init__(self, max_size: int = 2048):
        """
        Initialize the texture atlas.

        Args:
            max_size: Maximum atlas dimension (width and height)
        """
        self.max_size = max_size
        self.atlas_surface: Optional[pygame.Surface] = None
        self.regions: Dict[str, Tuple[int, int, int, int]] = {}  # name -> (x, y, w, h)
        self.current_y = 0
        self.current_x = 0
        self.row_height = 0
        self.scales = [0.5, 1.0, 1.5, 2.0]  # Pre-generate at these scales

    def _pack_rect(self, width: int, height: int) -> Optional[Tuple[int, int]]:
        """
        Find space in the atlas for a rectangle using simple row-based packing.

        Args:
            width: Rectangle width
            height: Rectangle height

        Returns:
            (x, y) position if space found, None otherwise
        """
        # Try to fit in current row
        if self.current_x + width <= self.max_size and self.current_y + height <= self.max_size:
            x, y = self.current_x, self.current_y
            self.current_x += width
            self.row_height = max(self.row_height, height)
            return (x, y)

        # Move to next row
        self.current_x = 0
        self.current_y += self.row_height
        self.row_height = 0

        # Try again in new row
        if self.current_x + width <= self.max_size and self.current_y + height <= self.max_size:
            x, y = self.current_x, self.current_y
            self.current_x += width
            self.row_height = max(self.row_height, height)
            return (x, y)

        # No space
        return None

    def add_texture(self, name: str, surface: pygame.Surface, scale: float = 1.0) -> bool:
        """
        Add a texture to the atlas at a specific scale.

        Args:
            name: Unique identifier (includes scale in key)
            surface: Source surface to add
            scale: Scale factor to apply

        Returns:
            True if added successfully, False if atlas is full
        """
        if self.atlas_surface is None:
            self.atlas_surface = pygame.Surface((self.max_size, self.max_size), pygame.SRCALPHA)

        # Scale the surface
        orig_w, orig_h = surface.get_size()
        scaled_w = max(1, int(orig_w * scale))
        scaled_h = max(1, int(orig_h * scale))

        scaled_surface = pygame.transform.smoothscale(surface, (scaled_w, scaled_h))

        # Find space in atlas
        pos = self._pack_rect(scaled_w, scaled_h)
        if pos is None:
            return False

        x, y = pos

        # Blit to atlas
        self.atlas_surface.blit(scaled_surface, (x, y))

        # Store region
        self.regions[name] = (x, y, scaled_w, scaled_h)

        return True

    def get_region(self, name: str) -> Optional[Tuple[int, int, int, int]]:
        """
        Get the atlas region for a named texture.

        Args:
            name: Texture identifier

        Returns:
            (x, y, width, height) tuple or None if not found
        """
        return self.regions.get(name)

    def get_subsurface(self, name: str) -> Optional[pygame.Surface]:
        """
        Get a subsurface of the atlas for a named texture.

        Args:
            name: Texture identifier

        Returns:
            Subsurface or None if not found
        """
        region = self.get_region(name)
        if region is None or self.atlas_surface is None:
            return None

        x, y, w, h = region
        try:
            return self.atlas_surface.subsurface((x, y, w, h))
        except:
            return None

    def build_from_respack(self, respack) -> Dict[str, List[str]]:
        """
        Build atlas from a respack's images.

        Args:
            respack: Respack object with img dictionary

        Returns:
            Dictionary mapping base names to list of scaled variants
        """
        texture_map: Dict[str, List[str]] = {}

        if not hasattr(respack, 'img') or not respack.img:
            return texture_map

        # Add each texture at multiple scales
        for img_name, img_surface in respack.img.items():
            if img_surface is None:
                continue

            # Skip very large textures (backgrounds, etc.)
            w, h = img_surface.get_size()
            if w > 512 or h > 512:
                continue

            variants = []

            for scale in self.scales:
                variant_name = f"{img_name}@{scale}x"

                if self.add_texture(variant_name, img_surface, scale):
                    variants.append(variant_name)
                else:
                    # Atlas full, stop adding
                    break

            if variants:
                texture_map[img_name] = variants

        return texture_map

    def get_closest_scale(self, base_name: str, target_scale: float, texture_map: Dict[str, List[str]]) -> Optional[str]:
        """
        Get the closest pre-scaled variant to the target scale.

        Args:
            base_name: Base texture name
            target_scale: Desired scale factor
            texture_map: Map of base names to scaled variants

        Returns:
            Variant name closest to target scale, or None
        """
        variants = texture_map.get(base_name)
        if not variants:
            return None

        # Find closest scale
        best_variant = None
        best_diff = float('inf')

        for variant in variants:
            # Extract scale from variant name (format: "name@1.5x")
            try:
                scale_str = variant.split('@')[1].rstrip('x')
                variant_scale = float(scale_str)
                diff = abs(variant_scale - target_scale)

                if diff < best_diff:
                    best_diff = diff
                    best_variant = variant
            except:
                continue

        return best_variant

    def get_atlas_surface(self) -> Optional[pygame.Surface]:
        """
        Get the atlas surface.

        Returns:
            The atlas surface containing all packed textures
        """
        return self.atlas_surface

    def get_stats(self) -> Dict[str, any]:
        """
        Get atlas statistics.

        Returns:
            Dictionary with texture count, utilization, etc.
        """
        total_area = self.max_size * self.max_size
        used_area = 0

        for _, (x, y, w, h) in self.regions.items():
            used_area += w * h

        utilization = (used_area / total_area * 100) if total_area > 0 else 0

        return {
            'texture_count': len(self.regions),
            'max_size': self.max_size,
            'utilization': utilization,
            'used_area': used_area,
            'total_area': total_area,
        }


# Global texture atlas instance
_global_atlas: Optional[TextureAtlas] = None
_global_texture_map: Dict[str, List[str]] = {}


def get_global_atlas() -> TextureAtlas:
    """
    Get the global texture atlas instance.

    Returns:
        The global TextureAtlas instance
    """
    global _global_atlas
    if _global_atlas is None:
        _global_atlas = TextureAtlas()
    return _global_atlas


def get_global_texture_map() -> Dict[str, List[str]]:
    """
    Get the global texture map.

    Returns:
        Dictionary mapping base texture names to scaled variants
    """
    return _global_texture_map


def set_global_texture_map(texture_map: Dict[str, List[str]]) -> None:
    """
    Set the global texture map.

    Args:
        texture_map: New texture map
    """
    global _global_texture_map
    _global_texture_map = texture_map


def reset_global_atlas() -> None:
    """Reset the global texture atlas."""
    global _global_atlas, _global_texture_map
    _global_atlas = None
    _global_texture_map = {}
