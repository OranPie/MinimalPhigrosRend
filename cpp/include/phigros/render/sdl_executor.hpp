#pragma once
#include "phigros/render/draw_list.hpp"
#include "phigros/render/texture.hpp"
#include "phigros/app/sdl_compat.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace phigros::render {

// Translates a DrawList into immediate SDL draw calls.
// Per-texture r/g/b/a/blend state is cached to skip redundant SDL API calls.
struct SdlExecutor {
    static void execute(SDL_Renderer* ren, const DrawList& dl) {
        // Per-texture state cache — avoids redundant set_color_mod / set_blend_mode calls
        struct TexState {
            SDL_Texture*  ptr   = nullptr;
            uint8_t r=255,g=255,b=255,a=255;
            SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
        };
        std::array<TexState, 16> cache{};
        int cache_sz = 0;

        auto get_state = [&](SDL_Texture* p) -> TexState& {
            for (int i = 0; i < cache_sz; ++i)
                if (cache[i].ptr == p) return cache[i];
            if (cache_sz < 16) { cache[cache_sz].ptr = p; return cache[cache_sz++]; }
            // Evict slot 0 (LRU approximation — rare with <16 textures)
            cache[0] = {p, 255, 255, 255, 255, SDL_BLENDMODE_BLEND};
            return cache[0];
        };

        for (const auto& c : dl.cmds) {
            switch (c.type) {
            case DrawCmd::Quad:
            case DrawCmd::QuadRegion: {
                if (!c.tex || !c.tex->tex) break;
                const Texture& tex = *c.tex;
                TexState& st = get_state(tex.tex);

                // Only call SDL when state actually changed
                if (c.r != st.r || c.g != st.g || c.b != st.b) {
                    tex.set_color_mod(c.r, c.g, c.b);
                    st.r = c.r; st.g = c.g; st.b = c.b;
                }
                if (c.a != st.a) {
                    tex.set_alpha_mod(c.a);
                    st.a = c.a;
                }
                if (c.blend != st.blend) {
                    tex.set_blend_mode(c.blend);
                    st.blend = c.blend;
                }

                SDL_FRect dst;
                dst.x = c.x - c.w * 0.5f;
                dst.y = c.y - c.h * 0.5f;
                dst.w = c.w;
                dst.h = c.h;

                double angle_deg = c.angle * (180.0 / M_PI);

                if (c.type == DrawCmd::QuadRegion) {
                    SDL_Rect src{c.sx, c.sy, c.sw, c.sh};
                    app::sdl::render_copy_ex(ren, tex.tex, &src, &dst,
                                             angle_deg, PHIGROS_FLIP_NONE);
                } else {
                    app::sdl::render_copy_ex(ren, tex.tex, nullptr, &dst,
                                             angle_deg, PHIGROS_FLIP_NONE);
                }
                break;
            }
            case DrawCmd::Rect: {
                app::sdl::set_draw_color(ren, c.r, c.g, c.b, c.a);
                SDL_FRect rect{c.x, c.y, c.w, c.h};
                app::sdl::render_fill_rect(ren, &rect);
                break;
            }
            case DrawCmd::Line: {
                double dx = c.x1 - c.x, dy = c.y1 - c.y;
                double len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.5f) break;
                app::sdl::set_draw_color(ren, c.r, c.g, c.b, c.a);
                double nx = -dy / len, ny = dx / len;
                int half = static_cast<int>(c.lw * 0.5f);
                for (int i = -half; i <= half; ++i) {
                    double off = static_cast<double>(i);
                    app::sdl::render_draw_line(ren,
                        static_cast<float>(c.x  + nx * off),
                        static_cast<float>(c.y  + ny * off),
                        static_cast<float>(c.x1 + nx * off),
                        static_cast<float>(c.y1 + ny * off));
                }
                break;
            }
            }
        }
    }
};

} // namespace phigros::render
