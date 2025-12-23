from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Tuple


ColorF = Tuple[float, float, float, float]


@dataclass
class Renderer2D:
    ctx: any
    size: tuple[int, int]
    prog: any
    vbo: any
    vao: any
    _vertex_capacity: int

    def begin_frame(self) -> None:
        w, h = self.size
        self.prog["u_size"].value = (float(w), float(h))

    def draw_triangles(self, vertices: bytes, vertex_count: int) -> None:
        if vertex_count <= 0:
            return
        if vertex_count > self._vertex_capacity:
            # grow to next power-ish
            cap = max(vertex_count, int(self._vertex_capacity * 1.5) + 1)
            self._realloc(cap)
        self.vbo.write(vertices)
        self.vao.render(mode=self.ctx.TRIANGLES, vertices=vertex_count)

    def _realloc(self, vertex_capacity: int) -> None:
        # Each vertex: vec2 pos (2f) + vec4 color (4f) => 6 floats => 24 bytes
        self._vertex_capacity = int(vertex_capacity)
        self.vbo = self.ctx.buffer(reserve=self._vertex_capacity * 24)
        self.vao = self.ctx.vertex_array(
            self.prog,
            [(self.vbo, "2f 4f", "in_pos", "in_color")],
        )


def create_renderer2d(ctx: any, *, size: tuple[int, int]) -> Renderer2D:
    vs = """
    #version 330
    in vec2 in_pos;
    in vec4 in_color;
    uniform vec2 u_size;
    uniform float u_weight;
    out vec4 v_color;
    void main() {
        vec2 ndc = vec2(
            (in_pos.x / u_size.x) * 2.0 - 1.0,
            1.0 - (in_pos.y / u_size.y) * 2.0
        );
        gl_Position = vec4(ndc, 0.0, 1.0);
        v_color = in_color;
    }
    """

    fs = """
    #version 330
    in vec4 v_color;
    uniform float u_weight;
    out vec4 f_color;
    void main() {
        f_color = v_color * u_weight;
    }
    """

    prog = ctx.program(vertex_shader=vs, fragment_shader=fs)
    # start with room for 4096 vertices
    vbo = ctx.buffer(reserve=4096 * 24)
    vao = ctx.vertex_array(prog, [(vbo, "2f 4f", "in_pos", "in_color")])

    return Renderer2D(ctx=ctx, size=size, prog=prog, vbo=vbo, vao=vao, _vertex_capacity=4096)
