$input  a_position  : POSITION;
$input  a_texcoord0 : TEXCOORD0;
$input  a_color0    : COLOR0;

$output v_texcoord0 : TEXCOORD0;
$output v_color0    : COLOR0;

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 0.0, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
