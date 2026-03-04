#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/io/respack.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace phigros::render {

struct HoldRenderer {
    double base_note_w = 0.0;
    double note_scale_x = 2.5;
    double note_scale_y = 1.0;

    void init(int W, int H, double scale_x, double scale_y) {
        base_note_w = 0.06 * W;
        note_scale_x = scale_x;
        note_scale_y = scale_y;
    }

    void draw(const SpriteBatch& batch,
              const io::Respack& respack,
              const std::vector<NoteSnapshot>& notes,
              double t) const {
        for (auto& ns : notes) {
            if (!ns.is_hold) continue;

            const auto& tex = respack.note_texture(3, false);
            if (!tex.valid()) continue;

            double ws = base_note_w * note_scale_x;

            // Head and tail positions from snapshot
            double hx = ns.wx, hy = ns.wy;
            double tx = ns.wx_tail, ty = ns.wy_tail;

            // Direction vector from tail to head
            double dx = hx - tx, dy = hy - ty;
            double total_len = std::sqrt(dx * dx + dy * dy);
            if (total_len < 1.0) continue;

            double angle = std::atan2(dy, dx) - M_PI * 0.5; // perpendicular to direction

            // Atlas dimensions
            int head_h = respack.cfg.hold_head_h;
            int tail_h = respack.cfg.hold_tail_h;
            int body_h = tex.h - head_h - tail_h;
            if (body_h <= 0) body_h = 1;

            // Scale factors: map texture pixels to screen pixels
            double px_per_texel = ws / tex.w;
            double head_screen_h = head_h * px_per_texel;
            double tail_screen_h = tail_h * px_per_texel;
            double body_screen_h = std::max(0.0, total_len - head_screen_h - tail_screen_h);

            // Color and alpha
            uint8_t r = ns.color.r, g = ns.color.g, b = ns.color.b;
            double alpha_f = ns.alpha * 255.0;
            if (ns.miss) { alpha_f *= 0.5; r = g = b = 128; }
            uint8_t a = static_cast<uint8_t>(std::clamp(alpha_f, 0.0, 255.0));
            if (a == 0) continue;

            // Direction unit vector (tail → head)
            double ux = dx / total_len, uy = dy / total_len;

            // Draw 3 slices along the direction vector

            // 1. Tail (at tail end)
            {
                double cx = tx + ux * tail_screen_h * 0.5;
                double cy = ty + uy * tail_screen_h * 0.5;
                SDL_Rect src{0, head_h + body_h, tex.w, tail_h};
                batch.draw_texture_region(tex,
                    0, head_h + body_h, tex.w, tail_h,
                    cx, cy, ws, tail_screen_h, angle, r, g, b, a);
            }

            // 2. Body (between tail and head)
            if (body_screen_h > 0.5) {
                double cx = tx + ux * (tail_screen_h + body_screen_h * 0.5);
                double cy = ty + uy * (tail_screen_h + body_screen_h * 0.5);
                batch.draw_texture_region(tex,
                    0, head_h, tex.w, body_h,
                    cx, cy, ws, body_screen_h, angle, r, g, b, a);
            }

            // Hold-glow: while this hold is actively pressed, draw a tinted
            // additive-blend overlay along the body to indicate active input.
            if (ns.holding && body_screen_h > 1.0) {
                double glow_cx = tx + ux * (tail_screen_h + body_screen_h * 0.5);
                double glow_cy = ty + uy * (tail_screen_h + body_screen_h * 0.5);
                uint8_t ga = static_cast<uint8_t>(a * 0.35);
                if (ga > 0) {
                    SDL_SetTextureBlendMode(tex.tex, SDL_BLENDMODE_ADD);
                    batch.draw_texture_region(tex,
                        0, head_h, tex.w, body_h,
                        glow_cx, glow_cy, ws * 1.15, body_screen_h,
                        angle, r, g, b, ga);
                    SDL_SetTextureBlendMode(tex.tex, SDL_BLENDMODE_BLEND);
                }
            }

            // 3. Head (at head end)
            {
                double cx = hx - ux * head_screen_h * 0.5;
                double cy = hy - uy * head_screen_h * 0.5;
                batch.draw_texture_region(tex,
                    0, 0, tex.w, head_h,
                    cx, cy, ws, head_screen_h, angle, r, g, b, a);
            }
        }
    }
};

} // namespace phigros::render
