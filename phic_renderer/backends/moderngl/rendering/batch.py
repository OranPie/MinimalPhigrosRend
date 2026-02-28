"""
Instanced batch renderer for ModernGL backend.

Renders thousands of sprites in a single draw call using GPU instancing.
Works with GLTextureManager's texture arrays for zero-cost texture switching.
"""

from __future__ import annotations

import struct
from typing import Any, List, Tuple
from dataclasses import dataclass

import numpy as np


@dataclass
class SpriteInstance:
    """Data for a single sprite instance."""
    pos: Tuple[float, float, float]  # x, y, layer_index
    rotation: float  # Rotation in radians
    scale: Tuple[float, float]  # (scale_x, scale_y)
    color: Tuple[float, float, float, float]  # RGBA (0.0-1.0)


class InstancedBatchRenderer:
    """
    GPU instanced sprite renderer.

    Renders thousands of sprites in a single draw call using OpenGL instancing.
    Each sprite can have different position, rotation, scale, color, and texture layer.
    """

    # Instanced vertex shader
    VERTEX_SHADER = """
    #version 330

    in vec2 in_pos;
    in vec2 in_uv;

    // Per-instance attributes
    in vec3 in_inst_pos;      // x, y, layer_index
    in float in_inst_rot;
    in vec2 in_inst_scale;
    in vec4 in_inst_color;

    out vec3 v_texcoord;      // u, v, layer
    out vec4 v_color;

    uniform mat4 u_projection;

    void main() {
        // Apply rotation and scale
        float c = cos(in_inst_rot);
        float s = sin(in_inst_rot);
        vec2 rotated = vec2(
            in_pos.x * c - in_pos.y * s,
            in_pos.x * s + in_pos.y * c
        );
        vec2 scaled = rotated * in_inst_scale;
        vec2 world_pos = scaled + in_inst_pos.xy;

        gl_Position = u_projection * vec4(world_pos, 0.0, 1.0);

        // Pass texture coordinates (u, v, layer)
        v_texcoord = vec3(in_uv, in_inst_pos.z);
        v_color = in_inst_color;
    }
    """

    # Fragment shader with texture array
    FRAGMENT_SHADER = """
    #version 330

    in vec3 v_texcoord;
    in vec4 v_color;

    out vec4 fragColor;

    uniform sampler2DArray u_texture_array;
    uniform float u_weight;

    void main() {
        vec4 tex_color = texture(u_texture_array, v_texcoord);
        fragColor = (tex_color * v_color) * u_weight;
    }
    """

    def __init__(self, ctx: Any, texture_manager: Any, max_instances: int = 10000):
        """
        Initialize instanced batch renderer.

        Args:
            ctx: ModernGL context
            texture_manager: GLTextureManager for texture arrays
            max_instances: Maximum sprites per batch
        """
        self.ctx = ctx
        self.texture_manager = texture_manager
        self.max_instances = max_instances

        # Create shader program
        self.program = ctx.program(
            vertex_shader=self.VERTEX_SHADER,
            fragment_shader=self.FRAGMENT_SHADER,
        )

        try:
            self.program['u_weight'].value = 1.0
        except Exception:
            pass

        # Base quad geometry (centered at origin, size 1x1)
        vertices = np.array([
            # pos(x,y), uv(u,v)
            -0.5, -0.5,  0.0, 0.0,
             0.5, -0.5,  1.0, 0.0,
             0.5,  0.5,  1.0, 1.0,
            -0.5, -0.5,  0.0, 0.0,
             0.5,  0.5,  1.0, 1.0,
            -0.5,  0.5,  0.0, 1.0,
        ], dtype='f4')

        self.vbo_quad = ctx.buffer(vertices)

        # Per-instance data buffer (dynamically updated each frame)
        # Each instance: 3f (pos+layer) + 1f (rot) + 2f (scale) + 4f (color) = 10 floats = 40 bytes
        self.instance_data: List[SpriteInstance] = []
        self.vbo_instances = ctx.buffer(reserve=max_instances * 40)

        # Vertex array object with instancing
        self.vao = ctx.vertex_array(
            self.program,
            [
                # Base quad geometry (per-vertex)
                (self.vbo_quad, '2f 2f', 'in_pos', 'in_uv'),
                # Per-instance attributes (/i = instanced)
                (self.vbo_instances, '3f 1f 2f 4f /i',
                 'in_inst_pos', 'in_inst_rot', 'in_inst_scale', 'in_inst_color'),
            ]
        )

        # Set up projection matrix (orthographic)
        self.window_size = (1920, 1080)  # Default, should be updated
        self._update_projection()

    def set_window_size(self, width: int, height: int):
        """
        Update window size and projection matrix.

        Args:
            width: Window width
            height: Window height
        """
        self.window_size = (width, height)
        self._update_projection()

    def set_weight(self, w: float) -> None:
        try:
            self.program['u_weight'].value = float(w)
        except Exception:
            pass

    def _update_projection(self):
        """Update orthographic projection matrix."""
        W, H = self.window_size

        # Orthographic projection matrix (flipped Y to match screen coordinates)
        # Maps (0, 0) to top-left, (W, H) to bottom-right
        proj = np.array([
            [2.0 / W,  0.0,       0.0, -1.0],
            [0.0,     -2.0 / H,   0.0,  1.0],
            [0.0,      0.0,      -1.0,  0.0],
            [0.0,      0.0,       0.0,  1.0],
        ], dtype='f4')

        self.program['u_projection'].write(proj.tobytes())

    def add_sprite(
        self,
        texture_name: str,
        pos: Tuple[float, float],
        rotation: float = 0.0,
        scale: Tuple[float, float] = (1.0, 1.0),
        color: Tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
    ):
        """
        Queue a sprite for batched rendering.

        Args:
            texture_name: Name of texture in texture manager
            pos: (x, y) position
            rotation: Rotation in radians
            scale: (width, height) scale
            color: RGBA color (0.0-1.0 range)
        """
        # Get texture layer
        layer = self.texture_manager.get_layer(texture_name)
        if layer is None:
            return  # Texture not loaded

        self.instance_data.append(SpriteInstance(
            pos=(pos[0], pos[1], float(layer)),
            rotation=rotation,
            scale=scale,
            color=color
        ))

    def flush(self):
        """
        Render all queued sprites in a single draw call and clear the batch.
        """
        if not self.instance_data:
            return

        # Pack instance data into buffer
        data = []
        for inst in self.instance_data:
            data.extend([
                inst.pos[0], inst.pos[1], inst.pos[2],  # 3 floats: x, y, layer
                inst.rotation,                           # 1 float
                inst.scale[0], inst.scale[1],           # 2 floats
                inst.color[0], inst.color[1], inst.color[2], inst.color[3]  # 4 floats
            ])

        instance_count = len(self.instance_data)

        # Write to GPU buffer
        self.vbo_instances.write(struct.pack(f'{len(data)}f', *data))

        # Bind texture array
        self.texture_manager.bind(location=0)
        self.program['u_texture_array'].value = 0

        # Instanced draw call (renders instance_count copies of the quad)
        self.vao.render(instances=instance_count)

        # Clear for next frame
        self.instance_data.clear()

    def clear(self):
        """Clear all queued sprites without rendering."""
        self.instance_data.clear()

    def get_stats(self) -> dict:
        """
        Get rendering statistics.

        Returns:
            Dictionary with instance count
        """
        return {
            'queued_sprites': len(self.instance_data),
            'max_instances': self.max_instances,
        }
