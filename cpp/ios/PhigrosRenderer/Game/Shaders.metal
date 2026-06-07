#include <metal_stdlib>
using namespace metal;

// Per-vertex input
struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texcoord;
    float4 color;
};

// Push-constants / uniforms per draw call
struct Uniforms {
    float2 screenSize;   // (width, height) in pixels
    float2 center;       // pivot in pixels
    float  cosA;         // cos(rotation)
    float  sinA;         // sin(rotation)
    float  scaleX;
    float  scaleY;
    int    useTexture;   // 1 = sample texture, 0 = solid color
};

vertex VertexOut vertex_main(VertexIn       in       [[stage_in]],
                             constant Uniforms& u    [[buffer(1)]]) {
    // Apply scale + rotation around center
    float2 local = float2(in.position.x * u.scaleX, in.position.y * u.scaleY);
    float2 rotated = float2(u.cosA * local.x - u.sinA * local.y,
                            u.sinA * local.x + u.cosA * local.y);
    float2 world = rotated + u.center;

    // NDC: origin top-left, y-down
    float2 ndc = float2(world.x / u.screenSize.x * 2.0 - 1.0,
                        1.0 - world.y / u.screenSize.y * 2.0);
    VertexOut out;
    out.position = float4(ndc, 0.0, 1.0);
    out.texcoord = in.texcoord;
    out.color    = in.color;
    return out;
}

fragment float4 fragment_main(VertexOut           in  [[stage_in]],
                               texture2d<float>   tex [[texture(0)]],
                               sampler            smp [[sampler(0)]],
                               constant Uniforms& u   [[buffer(1)]]) {
    if (u.useTexture == 1) {
        float4 sampled = tex.sample(smp, in.texcoord);
        return sampled * in.color;
    }
    return in.color;
}
