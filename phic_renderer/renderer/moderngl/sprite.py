from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Any, Iterable, Tuple

from .texture import GLTexture


@dataclass
class SpriteProgram:
    prog: Any
    vbo: Any
    vao: Any
    _vertex_capacity: int


def create_sprite_program(ctx: Any) -> SpriteProgram:
    vs = """
    #version 330
    in vec2 in_pos;
    in vec2 in_uv;
    in vec4 in_color;
    uniform vec2 u_size;
    out vec2 v_uv;
    out vec4 v_color;
    void main() {
        vec2 ndc = vec2(
            (in_pos.x / u_size.x) * 2.0 - 1.0,
            1.0 - (in_pos.y / u_size.y) * 2.0
        );
        gl_Position = vec4(ndc, 0.0, 1.0);
        v_uv = in_uv;
        v_color = in_color;
    }
    """

    fs = """
    #version 330
    uniform sampler2D u_tex;
    uniform float u_weight;
    in vec2 v_uv;
    in vec4 v_color;
    out vec4 f_color;
    void main() {
        vec4 t = texture(u_tex, v_uv);
        f_color = (t * v_color) * u_weight;
    }
    """

    prog = ctx.program(vertex_shader=vs, fragment_shader=fs)
    vbo = ctx.buffer(reserve=4096 * 32)  # 8 floats => 32 bytes
    vao = ctx.vertex_array(prog, [(vbo, "2f 2f 4f", "in_pos", "in_uv", "in_color")])
    return SpriteProgram(prog=prog, vbo=vbo, vao=vao, _vertex_capacity=4096)


def _ensure_capacity(ctx: Any, sp: SpriteProgram, vertex_count: int) -> None:
    if vertex_count <= sp._vertex_capacity:
        return
    cap = max(vertex_count, int(sp._vertex_capacity * 1.5) + 1)
    sp._vertex_capacity = int(cap)
    sp.vbo = ctx.buffer(reserve=sp._vertex_capacity * 32)
    sp.vao = ctx.vertex_array(sp.prog, [(sp.vbo, "2f 2f 4f", "in_pos", "in_uv", "in_color")])


def draw_textured_quad(
    *,
    ctx: Any,
    sp: SpriteProgram,
    tex: GLTexture,
    window_size: Tuple[int, int],
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    rgba: Tuple[int, int, int, int] = (255, 255, 255, 255),
) -> None:
    W, H = window_size
    sp.prog["u_size"].value = (float(W), float(H))

    r, g, b, a = rgba
    cf = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)

    # UVs: because texture is flipped in load_texture_rgba (flip_y=True), we can use normal uv.
    verts = [
        (x0, y0, 0.0, 0.0, *cf),
        (x1, y0, 1.0, 0.0, *cf),
        (x1, y1, 1.0, 1.0, *cf),
        (x0, y0, 0.0, 0.0, *cf),
        (x1, y1, 1.0, 1.0, *cf),
        (x0, y1, 0.0, 1.0, *cf),
    ]
    data = b"".join(struct.pack("8f", *v) for v in verts)

    _ensure_capacity(ctx, sp, 6)
    sp.vbo.write(data)

    tex.tex.use(location=0)
    try:
        sp.prog["u_tex"].value = 0
    except:
        pass
    sp.vao.render(mode=ctx.TRIANGLES, vertices=6)


def draw_textured_quad_pts_uv(
    *,
    ctx: Any,
    sp: SpriteProgram,
    tex: GLTexture,
    window_size: Tuple[int, int],
    pts: Iterable[Tuple[float, float]],
    uv0: Tuple[float, float],
    uv1: Tuple[float, float],
    rgba: Tuple[int, int, int, int] = (255, 255, 255, 255),
) -> None:
    W, H = window_size
    sp.prog["u_size"].value = (float(W), float(H))

    p = list(pts)
    if len(p) != 4:
        return
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = p

    (u0, v0) = uv0
    (u1, v1) = uv1

    r, g, b, a = rgba
    cf = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)

    verts = [
        (x0, y0, float(u0), float(v0), *cf),
        (x1, y1, float(u1), float(v0), *cf),
        (x2, y2, float(u1), float(v1), *cf),
        (x0, y0, float(u0), float(v0), *cf),
        (x2, y2, float(u1), float(v1), *cf),
        (x3, y3, float(u0), float(v1), *cf),
    ]
    data = b"".join(struct.pack("8f", *v) for v in verts)

    _ensure_capacity(ctx, sp, 6)
    sp.vbo.write(data)

    tex.tex.use(location=0)
    try:
        sp.prog["u_tex"].value = 0
    except:
        pass
    sp.vao.render(mode=ctx.TRIANGLES, vertices=6)


def draw_textured_quad_pts(
    *,
    ctx: Any,
    sp: SpriteProgram,
    tex: GLTexture,
    window_size: Tuple[int, int],
    pts: Iterable[Tuple[float, float]],
    rgba: Tuple[int, int, int, int] = (255, 255, 255, 255),
) -> None:
    W, H = window_size
    sp.prog["u_size"].value = (float(W), float(H))

    p = list(pts)
    if len(p) != 4:
        return
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = p

    r, g, b, a = rgba
    cf = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)

    verts = [
        (x0, y0, 0.0, 0.0, *cf),
        (x1, y1, 1.0, 0.0, *cf),
        (x2, y2, 1.0, 1.0, *cf),
        (x0, y0, 0.0, 0.0, *cf),
        (x2, y2, 1.0, 1.0, *cf),
        (x3, y3, 0.0, 1.0, *cf),
    ]
    data = b"".join(struct.pack("8f", *v) for v in verts)

    _ensure_capacity(ctx, sp, 6)
    sp.vbo.write(data)

    tex.tex.use(location=0)
    try:
        sp.prog["u_tex"].value = 0
    except:
        pass
    sp.vao.render(mode=ctx.TRIANGLES, vertices=6)
