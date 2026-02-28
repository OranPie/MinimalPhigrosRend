"""
GPU trail effect for ModernGL backend.

Implements motion trails using framebuffer history and GPU blending.
Much faster than CPU-based surface blending approaches.
"""

from __future__ import annotations

from typing import Any

import numpy as np


class GPUTrailEffect:
    """
    GPU-based trail effect using framebuffer history.

    Captures frames to a ring buffer of framebuffers, then renders
    previous frames with decay and blur on GPU for smooth trail effects.
    """

    # Vertex shader for fullscreen quad
    VERTEX_SHADER = """
    #version 330
    in vec2 in_pos;
    out vec2 v_uv;

    void main() {
        gl_Position = vec4(in_pos, 0.0, 1.0);
        v_uv = in_pos * 0.5 + 0.5;
    }
    """

    # Fragment shader with blur
    FRAGMENT_SHADER = """
    #version 330
    in vec2 v_uv;
    out vec4 fragColor;

    uniform sampler2D u_texture;
    uniform float u_alpha;
    uniform float u_blur;
    uniform int u_dim;  // Additional dimming (0-255)

    void main() {
        vec2 texel_size = 1.0 / textureSize(u_texture, 0);
        vec4 color = vec4(0.0);

        // Box blur with variable sample count
        int samples = int(u_blur * 3.0) + 1;
        float total_weight = 0.0;

        for (int x = -samples; x <= samples; x++) {
            for (int y = -samples; y <= samples; y++) {
                vec2 offset = vec2(x, y) * texel_size * u_blur;
                float weight = 1.0;
                color += texture(u_texture, v_uv + offset) * weight;
                total_weight += weight;
            }
        }

        color /= total_weight;

        // Apply dimming if requested
        if (u_dim > 0) {
            float dim_alpha = float(u_dim) / 255.0;
            color.rgb = mix(color.rgb, vec3(0.0), dim_alpha);
        }

        // Apply alpha decay
        color.a *= u_alpha;

        fragColor = color;
    }
    """

    def __init__(self, ctx: Any, width: int, height: int, max_frames: int = 8):
        """
        Initialize GPU trail effect.

        Args:
            ctx: ModernGL context
            width: Framebuffer width
            height: Framebuffer height
            max_frames: Maximum frames to keep in history
        """
        self.ctx = ctx
        self.width = width
        self.height = height
        self.max_frames = max_frames

        # Create framebuffer ring buffer
        self.fbos = []
        self.textures = []
        for _ in range(max_frames):
            tex = ctx.texture((width, height), 4)  # RGBA
            try:
                import moderngl  # type: ignore
                tex.filter = (moderngl.LINEAR, moderngl.LINEAR)
            except:
                pass
            fbo = ctx.framebuffer(color_attachments=[tex])
            self.fbos.append(fbo)
            self.textures.append(tex)

        self.current_idx = 0
        self.frame_count = 0

        # Create blur shader program
        self.blur_program = ctx.program(
            vertex_shader=self.VERTEX_SHADER,
            fragment_shader=self.FRAGMENT_SHADER,
        )

        # Create fullscreen quad VAO
        self.blend_vao = self._create_fullscreen_quad()

    def _create_fullscreen_quad(self):
        """Create VAO for fullscreen quad."""
        vertices = np.array([
            -1.0, -1.0,
             1.0, -1.0,
             1.0,  1.0,
            -1.0, -1.0,
             1.0,  1.0,
            -1.0,  1.0,
        ], dtype='f4')

        vbo = self.ctx.buffer(vertices)
        vao = self.ctx.vertex_array(self.blur_program, [(vbo, '2f', 'in_pos')])
        return vao

    def capture_frame(self, source_fbo: Any):
        """
        Capture current frame to history ring buffer.

        Args:
            source_fbo: Source framebuffer to copy from
        """
        target_fbo = self.fbos[self.current_idx]

        # Copy source to target
        self.ctx.copy_framebuffer(target_fbo, source_fbo)

        # Advance ring buffer
        self.current_idx = (self.current_idx + 1) % self.max_frames
        self.frame_count = min(self.frame_count + 1, self.max_frames)

    def render_trail(
        self,
        target_fbo: Any,
        alpha: float = 0.8,
        decay: float = 0.9,
        blur_amount: float = 1.0,
        blur_ramp: bool = True,
        dim: int = 0,
        additive: bool = False,
    ):
        """
        Render trail effect to target framebuffer.

        Args:
            target_fbo: Target framebuffer to render to
            alpha: Overall trail alpha (0.0-1.0)
            decay: Per-frame decay factor (0.0-1.0)
            blur_amount: Blur intensity multiplier
            blur_ramp: Whether to increase blur with age
            dim: Additional dimming (0-255, 0=no dim)
            additive: Use additive blending instead of alpha blending
        """
        if self.frame_count == 0:
            return

        target_fbo.use()

        # Set blend mode
        try:
            import moderngl  # type: ignore

            self.ctx.enable(moderngl.BLEND)

            if additive:
                self.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE)
            else:
                self.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA)
        except:
            pass

        # Render frames from oldest to newest
        for i in range(self.frame_count):
            age = self.frame_count - i - 1
            frame_idx = (self.current_idx - 1 - age) % self.max_frames

            # Calculate alpha with exponential decay
            frame_alpha = alpha * (decay ** age)
            if frame_alpha < 0.001:
                continue

            # Bind frame texture
            self.textures[frame_idx].use(location=0)

            # Calculate blur (increase with age if ramp enabled)
            blur_val = blur_amount
            if blur_ramp:
                blur_val = blur_amount * (1.0 + age * 0.5)

            # Set shader uniforms
            self.blur_program['u_texture'].value = 0
            self.blur_program['u_alpha'].value = frame_alpha
            self.blur_program['u_blur'].value = blur_val
            self.blur_program['u_dim'].value = int(dim)

            # Render fullscreen quad
            self.blend_vao.render()

        # Restore default blend mode
        try:
            import moderngl  # type: ignore
            self.ctx.disable(moderngl.BLEND)
        except:
            pass

    def clear(self):
        """Clear frame history."""
        self.current_idx = 0
        self.frame_count = 0

    def release(self):
        """Release GPU resources."""
        for tex in self.textures:
            try:
                tex.release()
            except:
                pass
        for fbo in self.fbos:
            try:
                fbo.release()
            except:
                pass

    def get_stats(self) -> dict:
        """
        Get trail effect statistics.

        Returns:
            Dictionary with frame count, buffer size, etc.
        """
        return {
            'frame_count': self.frame_count,
            'max_frames': self.max_frames,
            'buffer_size': (self.width, self.height),
        }
