#pragma once
// Embedded GLSL shaders for OpenGL / OpenGL ES (runtime compilation fallback).
// For Vulkan/Metal/DX, use shaderc-compiled SPIR-V/MSL/HLSL from the shaders/ dir.
// These GLSL versions are sufficient for desktop OpenGL 3.3+ and WebGL2/GLES3.

namespace phigros::render::shaders {

// Vertex shader: 2D sprite with position, texcoord, color
inline constexpr const char* vs_sprite_glsl = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec2 a_position;
attribute vec2 a_texcoord0;
attribute vec4 a_color0;

varying vec2 v_texcoord0;
varying vec4 v_color0;

uniform mat4 u_viewProj;

void main() {
    gl_Position = u_viewProj * vec4(a_position, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
)";

// Fragment shader: textured sprite with color modulation
inline constexpr const char* fs_sprite_glsl = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texcoord0;
varying vec4 v_color0;

uniform sampler2D s_texColor;

void main() {
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    gl_FragColor  = texColor * v_color0;
}
)";

// Vertex shader version for OpenGL 3.3+ (GLSL 330)
inline constexpr const char* vs_sprite_gl33 = R"(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord0;
layout(location = 2) in vec4 a_color0;

out vec2 v_texcoord0;
out vec4 v_color0;

uniform mat4 u_viewProj;

void main() {
    gl_Position = u_viewProj * vec4(a_position, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
)";

inline constexpr const char* fs_sprite_gl33 = R"(
#version 330 core
in vec2 v_texcoord0;
in vec4 v_color0;
out vec4 FragColor;

uniform sampler2D s_texColor;

void main() {
    vec4 texColor = texture(s_texColor, v_texcoord0);
    FragColor = texColor * v_color0;
}
)";

} // namespace phigros::render::shaders
