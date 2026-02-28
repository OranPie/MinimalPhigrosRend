"""
GPU texture array manager for ModernGL backend.

Uses OpenGL texture arrays (sampler2DArray) for efficient sprite rendering
with zero-cost texture switching. All textures in an array must have the
same dimensions, so textures are resized to a common layer size.
"""

from __future__ import annotations

import logging
from typing import Any, Dict, Optional, Tuple
import os

try:
    from PIL import Image  # type: ignore
except:
    Image = None


class GLTextureManager:
    """
    GPU texture array manager for efficient sprite rendering.

    Uses sampler2DArray to store multiple textures in a single GPU resource,
    enabling rendering of thousands of sprites with different textures in a
    single draw call via instancing.

    All textures are normalized to the same layer_size dimensions.
    """

    def __init__(self, ctx: Any, layer_size: Tuple[int, int] = (256, 256), max_layers: int = 64):
        """
        Initialize texture array manager.

        Args:
            ctx: ModernGL context
            layer_size: Size of each texture layer (width, height)
            max_layers: Maximum number of texture layers
        """
        if Image is None:
            raise SystemExit(
                "Texture array manager requires Pillow. Install it: pip install pillow"
            )

        self.ctx = ctx
        self.layer_size = layer_size
        self.max_layers = max_layers

        # Create texture array (3D texture with depth = max_layers)
        self.texture_array = ctx.texture_array(
            size=(layer_size[0], layer_size[1], max_layers),
            components=4,  # RGBA
        )

        # Set texture filtering
        try:
            import moderngl  # type: ignore
            self.texture_array.filter = (moderngl.LINEAR, moderngl.LINEAR)
        except:
            pass

        # Texture name → layer index mapping
        self.layer_map: Dict[str, int] = {}
        self.next_layer = 0

    def add_texture_from_pil(self, name: str, pil_image: Image.Image) -> int:
        """
        Add a PIL image to the texture array.

        Args:
            name: Unique texture identifier
            pil_image: PIL Image to add

        Returns:
            Layer index in texture array

        Raises:
            RuntimeError: If texture array is full
        """
        # Check if already added
        if name in self.layer_map:
            return self.layer_map[name]

        # Check if array is full
        if self.next_layer >= self.max_layers:
            raise RuntimeError(f"Texture array full (max {self.max_layers} layers)")

        # Resize to layer_size
        resized = pil_image.resize(self.layer_size, Image.Resampling.LANCZOS)

        # Convert to RGBA bytes
        rgba_data = resized.convert('RGBA').tobytes()

        # Write to specific layer
        layer_idx = self.next_layer
        self.texture_array.write(
            rgba_data,
            viewport=(0, 0, self.layer_size[0], self.layer_size[1], layer_idx, 1)
        )

        # Update mapping
        self.layer_map[name] = layer_idx
        self.next_layer += 1

        return layer_idx

    def add_texture_from_respack(self, respack: Any, tex_name: str) -> Optional[int]:
        """
        Add a texture from a respack to the texture array.

        Args:
            respack: Resource pack object with img dictionary
            tex_name: Texture name (e.g., "click.png")

        Returns:
            Layer index, or None if texture not found in respack
        """
        if not hasattr(respack, 'img') or not respack.img:
            return None

        try:
            tmpdir = getattr(respack, "tmpdir", None)
            base_dir = getattr(tmpdir, "name", None) if tmpdir is not None else None
            if base_dir:
                fp = os.path.join(str(base_dir), str(tex_name))
                if os.path.exists(fp):
                    return self.add_texture_from_file(tex_name, fp)
        except Exception:
            pass

        tex = None
        try:
            tex = respack.img.get(tex_name)
        except Exception:
            tex = None
        if tex is None:
            return None

        # NOTE: ModernGL respack loader stores GLTexture objects in respack.img.
        # We cannot reliably extract PIL pixels from those without readback.
        return None

    def add_texture_from_file(self, name: str, file_path: str) -> int:
        if Image is None:
            raise SystemExit(
                "Texture array manager requires Pillow. Install it: pip install pillow"
            )
        img = Image.open(str(file_path)).convert("RGBA")
        try:
            img = img.transpose(Image.FLIP_TOP_BOTTOM)
        except Exception:
            pass
        return self.add_texture_from_pil(name, img)

    def load_respack_textures(self, respack: Any, texture_names: list[str]) -> Dict[str, int]:
        """
        Load multiple textures from respack.

        Args:
            respack: Resource pack object
            texture_names: List of texture names to load

        Returns:
            Dictionary mapping texture names to layer indices
        """
        result = {}
        for tex_name in texture_names:
            layer_idx = self.add_texture_from_respack(respack, tex_name)
            if layer_idx is not None:
                result[tex_name] = layer_idx
        return result

    def get_layer(self, name: str) -> Optional[int]:
        """
        Get layer index for a texture name.

        Args:
            name: Texture identifier

        Returns:
            Layer index, or None if not found
        """
        return self.layer_map.get(name)

    def bind(self, location: int = 0):
        """
        Bind texture array to a shader sampler location.

        Args:
            location: Sampler location in shader (default 0)
        """
        self.texture_array.use(location)

    def clear(self):
        """Clear all texture mappings (does not release GPU memory)."""
        self.layer_map.clear()
        self.next_layer = 0

    def release(self):
        """Release GPU texture array."""
        try:
            self.texture_array.release()
        except:
            pass

    def get_stats(self) -> Dict[str, any]:
        """
        Get texture array statistics.

        Returns:
            Dictionary with texture count, layer size, utilization, etc.
        """
        utilization = (self.next_layer / self.max_layers * 100) if self.max_layers > 0 else 0

        return {
            'texture_count': len(self.layer_map),
            'next_layer': self.next_layer,
            'max_layers': self.max_layers,
            'layer_size': self.layer_size,
            'utilization': utilization,
        }


def create_texture_manager_from_respack(
    ctx: Any,
    respack: Any,
    layer_size: Tuple[int, int] = (256, 256),
    max_layers: int = 64
) -> GLTextureManager:
    logger = logging.getLogger(__name__)
    """
    Create and populate a texture manager from a respack.

    Args:
        ctx: ModernGL context
        respack: Resource pack with note textures
        layer_size: Texture layer size
        max_layers: Maximum number of layers

    Returns:
        Populated GLTextureManager
    """
    # If possible, infer layer size from actual image files to avoid unnecessary resampling.
    try:
        tmpdir = getattr(respack, "tmpdir", None)
        base_dir = getattr(tmpdir, "name", None) if tmpdir is not None else None
        if base_dir:
            fp0 = os.path.join(str(base_dir), "click.png")
            if Image is not None and os.path.exists(fp0):
                im0 = Image.open(fp0)
                layer_size = (int(im0.size[0]), int(im0.size[1]))
    except Exception:
        pass

    try:
        logger.info("[Batch] TextureArray layer_size=%s max_layers=%s", str(layer_size), int(max_layers))
    except Exception:
        pass

    manager = GLTextureManager(ctx, layer_size, max_layers)

    # Standard note textures
    standard_textures = [
        "click.png",
        "click_mh.png",
        "drag.png",
        "drag_mh.png",
        "flick.png",
        "flick_mh.png",
        "hold.png",
        "hold_mh.png",
    ]

    loaded = manager.load_respack_textures(respack, standard_textures)

    try:
        st = manager.get_stats()
        logger.info(
            "[Batch] TextureArray loaded=%d util=%.1f%%",
            int(st.get("texture_count", 0) or 0),
            float(st.get("utilization", 0.0) or 0.0),
        )
    except Exception:
        pass

    return manager
