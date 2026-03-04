#pragma once
// BgfxExecutor: executes a DrawList using the bgfx GPU backend.
// Only compiled when PHIGROS_HAS_BGFX is defined.
//
// Texture bridging: bgfx cannot directly use SDL_Texture* handles.
// This executor maintains a cache mapping SDL_Texture* → bgfx TextureHandle.
// Textures are lazily uploaded from the SDL texture's pixel data on first use.
// Note: SDL_TEXTUREACCESS_TARGET textures cannot be read back; they are skipped.

#ifdef PHIGROS_HAS_BGFX

#include "phigros/render/draw_list.hpp"
#include "phigros/render/bgfx_renderer.hpp"
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cmath>

namespace phigros::render {

struct BgfxExecutor {
    BgfxRenderer renderer;

    // SDL_Texture* → bgfx TextureHandle (lazy upload cache)
    std::unordered_map<SDL_Texture*, bgfx::TextureHandle> tex_cache;

    void init(int w, int h) {
        renderer.init(nullptr, w, h);
    }

    bgfx::TextureHandle get_bgfx_tex(const Texture* t) {
        if (!t || !t->tex) return BGFX_INVALID_HANDLE;
        auto it = tex_cache.find(t->tex);
        if (it != tex_cache.end()) return it->second;

        // Upload from pixel cache (populated during respack load via from_memory_cached)
        if (!t->pixel_data || t->pixel_data->empty()) return BGFX_INVALID_HANDLE;
        int w = t->w, h = t->h;
        if (w <= 0 || h <= 0) return BGFX_INVALID_HANDLE;

        const bgfx::Memory* mem = bgfx::copy(t->pixel_data->data(), (uint32_t)(w * h * 4));
        bgfx::TextureHandle bh = bgfx::createTexture2D(
            (uint16_t)w, (uint16_t)h, false, 1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, mem);

        tex_cache[t->tex] = bh;
        return bh;
    }

    void execute(const DrawList& dl) {
        renderer.begin_frame();
        for (const auto& c : dl.cmds) {
            switch (c.type) {
            case DrawCmd::Quad:
            case DrawCmd::QuadRegion: {
                if (!c.tex) break;
                bgfx::TextureHandle bh = get_bgfx_tex(c.tex);
                if (!bgfx::isValid(bh)) {
                    // Fallback: skip (texture not uploadable via lock)
                    break;
                }
                // Map to IRenderer TextureHandle
                TextureHandle th = renderer.register_external(bh, c.tex->w, c.tex->h);
                renderer.set_color(c.r, c.g, c.b, c.a);
                if (c.type == DrawCmd::QuadRegion) {
                    float u0 = (float)c.sx / c.tex->w;
                    float v0 = (float)c.sy / c.tex->h;
                    float u1 = (float)(c.sx + c.sw) / c.tex->w;
                    float v1 = (float)(c.sy + c.sh) / c.tex->h;
                    renderer.draw_quad_uv(th, c.x, c.y, c.w, c.h, c.angle, u0, v0, u1, v1);
                } else {
                    renderer.draw_quad(th, c.x, c.y, c.w, c.h, c.angle);
                }
                break;
            }
            case DrawCmd::Rect: {
                renderer.draw_rect(c.x, c.y, c.w, c.h, c.r, c.g, c.b, c.a);
                break;
            }
            case DrawCmd::Line: {
                double dx = c.x1 - c.x, dy = c.y1 - c.y;
                double len = std::sqrt(dx * dx + dy * dy);
                double mx = (c.x + c.x1) * 0.5, my = (c.y + c.y1) * 0.5;
                double angle = std::atan2(dy, dx) - M_PI * 0.5;
                // Draw line as a thin quad
                if (renderer.white_handle != INVALID_TEXTURE)
                    renderer.draw_quad(renderer.white_handle,
                        (float)mx, (float)my, (float)c.lw, (float)len,
                        (float)angle);
                break;
            }
            }
        }
        renderer.end_frame();
    }

    void destroy() {
        for (auto& [sdl, bh] : tex_cache)
            if (bgfx::isValid(bh)) bgfx::destroy(bh);
        tex_cache.clear();
        renderer.shutdown();
    }
};

} // namespace phigros::render

#endif // PHIGROS_HAS_BGFX
