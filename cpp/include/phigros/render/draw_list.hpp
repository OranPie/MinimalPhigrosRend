#pragma once
#include "phigros/app/sdl_compat.hpp"
#include "phigros/render/texture.hpp"
#include <vector>
#include <cstdint>

namespace phigros::render {

// Tagged draw command emitted by sub-renderers and executed by a backend executor.
struct DrawCmd {
    enum Type : uint8_t { Quad, QuadRegion, Rect, Line } type = Quad;

    const Texture* tex = nullptr;  // Quad / QuadRegion texture (lifetime: within frame)

    // Quad / QuadRegion / Rect
    float x = 0, y = 0, w = 0, h = 0;
    float angle = 0;  // radians

    // QuadRegion source rectangle (texture atlas slice)
    int sx = 0, sy = 0, sw = 0, sh = 0;

    // Line second endpoint and width
    float x1 = 0, y1 = 0, lw = 1;

    // Colour modulation + alpha
    uint8_t r = 255, g = 255, b = 255, a = 255;

    // Per-command blend mode (captured from SpriteBatch::current_blend at emit time)
    SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
};

// Ordered command buffer built by sub-renderers each frame.
// The executor (SdlExecutor / BgfxExecutor) iterates and dispatches to the GPU.
struct DrawList {
    std::vector<DrawCmd> cmds;

    void clear()       { cmds.clear(); }
    void reserve(int n){ cmds.reserve(static_cast<size_t>(n)); }

    void quad(const Texture* t, float x, float y, float w, float h, float angle,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a,
              SDL_BlendMode blend = SDL_BLENDMODE_BLEND) {
        cmds.push_back({DrawCmd::Quad, t, x, y, w, h, angle, 0,0,0,0, 0,0,0, r,g,b,a, blend});
    }

    void quad_region(const Texture* t, int sx, int sy, int sw, int sh,
                     float x, float y, float w, float h, float angle,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                     SDL_BlendMode blend = SDL_BLENDMODE_BLEND) {
        DrawCmd c{};
        c.type = DrawCmd::QuadRegion;
        c.tex = t;
        c.x = x; c.y = y; c.w = w; c.h = h; c.angle = angle;
        c.sx = sx; c.sy = sy; c.sw = sw; c.sh = sh;
        c.r = r; c.g = g; c.b = b; c.a = a;
        c.blend = blend;
        cmds.push_back(c);
    }

    void rect(float x, float y, float w, float h,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        DrawCmd c{}; c.type = DrawCmd::Rect;
        c.x = x; c.y = y; c.w = w; c.h = h;
        c.r = r; c.g = g; c.b = b; c.a = a;
        cmds.push_back(c);
    }

    void line(float x0, float y0, float x1, float y1, float lw,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        DrawCmd c{}; c.type = DrawCmd::Line;
        c.x = x0; c.y = y0; c.x1 = x1; c.y1 = y1; c.lw = lw;
        c.r = r; c.g = g; c.b = b; c.a = a;
        cmds.push_back(c);
    }
};

} // namespace phigros::render
