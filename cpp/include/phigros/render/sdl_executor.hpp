#pragma once
#include "phigros/render/draw_list.hpp"
#include "phigros/render/texture.hpp"
#include "phigros/app/sdl_compat.hpp"
#include <cmath>
#include <algorithm>

namespace phigros::render {

// Translates a DrawList into immediate SDL draw calls.
// Behaviorally identical to the direct SpriteBatch path — used to validate
// that the DrawList architecture produces the same output as the old path.
struct SdlExecutor {
    // Execute all commands in the DrawList on the given SDL_Renderer.
    static void execute(SDL_Renderer* ren, const DrawList& dl) {
        SDL_BlendMode cur_tex_blend = SDL_BLENDMODE_BLEND;

        for (const auto& c : dl.cmds) {
            switch (c.type) {
            case DrawCmd::Quad:
            case DrawCmd::QuadRegion: {
                if (!c.tex || !c.tex->tex) break;
                const Texture& tex = *c.tex;
                tex.set_color_mod(c.r, c.g, c.b);
                tex.set_alpha_mod(c.a);
                if (c.blend != cur_tex_blend) {
                    tex.set_blend_mode(c.blend);
                    cur_tex_blend = c.blend;
                } else {
                    tex.set_blend_mode(c.blend);
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
