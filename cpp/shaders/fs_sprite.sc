$input v_texcoord0 : TEXCOORD0;
$input v_color0    : COLOR0;

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    gl_FragColor  = texColor * v_color0;
}
