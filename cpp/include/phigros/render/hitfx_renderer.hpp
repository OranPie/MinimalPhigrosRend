#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"   // apply_expand_xy
#include "phigros/render/texture.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/io/respack.hpp"
#include <cmath>
#include <algorithm>

namespace phigros::render {

struct HitFXRenderer {

    // Approximate a circle outline using N line segments rendered as quads.
    static void draw_circle_outline(const SpriteBatch& batch,
                                    const Texture& white_tex,
                                    double cx, double cy, double r,
                                    uint8_t rr, uint8_t gg, uint8_t bb, uint8_t a,
                                    int n = 14) {
        if (r < 1.0 || a == 0) return;
        static constexpr double LINE_W = 2.0;
        double step = 2.0 * M_PI / n;
        for (int i = 0; i < n; ++i) {
            double a0 = i * step, a1 = (i + 1) * step;
            double x0 = cx + r * std::cos(a0), y0 = cy + r * std::sin(a0);
            double x1 = cx + r * std::cos(a1), y1 = cy + r * std::sin(a1);
            double mx = (x0 + x1) * 0.5, my = (y0 + y1) * 0.5;
            double dx = x1 - x0, dy = y1 - y0;
            double seg_len = std::sqrt(dx * dx + dy * dy);
            double angle = std::atan2(dy, dx);
            batch.draw_rotated_rect(white_tex, mx, my, seg_len, LINE_W,
                                    angle, rr, gg, bb, a);
        }
    }

    void draw(const SpriteBatch& batch,
              const io::Respack& respack,
              const engine::EffectManager& effects,
              double t,
              bool show_hitfx = true,
              bool show_particles = true,
              float intensity = 1.0f,
              int W = 1280, int H = 720, double expand = 1.0) const {

        const auto& sheet = respack.hitfx_sheet;
        bool has_sheet = sheet.valid();
        int cols = respack.cfg.hitfx_cols;
        int rows = respack.cfg.hitfx_rows;
        int total_frames = cols * rows;
        double duration = respack.cfg.hitfx_duration;
        double scale = respack.cfg.hitfx_scale;

        int cell_w = (has_sheet && cols > 0) ? (sheet.w / cols) : 0;
        int cell_h = (has_sheet && rows > 0) ? (sheet.h / rows) : 0;
        double draw_size = (cell_w > 0) ? (cell_w * scale) : 0.0;

        if (show_hitfx) {
            // ── Spritesheet hit effects ──────────────────────────────────────
            if (has_sheet && cell_w > 0 && cell_h > 0 && total_frames > 0) {
                for (const auto& fx : effects.hitfx) {
                    double age = t - fx.t0;
                    if (age < 0 || age > duration) continue;

                    double p = std::clamp(age / duration, 0.0, 0.999);
                    int frame_idx = std::clamp(static_cast<int>(p * total_frames),
                                               0, total_frames - 1);
                    int ix = frame_idx % cols;
                    int iy = frame_idx / cols;

                    uint8_t cr = fx.rgba.r, cg = fx.rgba.g, cb = fx.rgba.b;
                    if (!respack.cfg.hitfx_tinted) { cr = 255; cg = 255; cb = 255; }

                    // Ease-out alpha: fast start, slow fade (matches visual impact)
                    double fade = std::pow(1.0 - p, 0.65);
                    uint8_t a = static_cast<uint8_t>(
                        std::clamp(255.0 * fade * double(intensity), 0.0, 255.0));

                    // Animated rotation (rot + rot_speed * age)
                    double rendered_rot = respack.cfg.hitfx_rotate
                        ? (fx.rot + fx.rot_speed * age) : 0.0;

                    double fx_x = fx.x, fx_y = fx.y;
                    apply_expand_xy(fx_x, fx_y, W, H, expand);
                    batch.draw_texture_region(sheet,
                        ix * cell_w, iy * cell_h, cell_w, cell_h,
                        fx_x, fx_y, draw_size, draw_size,
                        rendered_rot, cr, cg, cb, a);
                }
            }

            // ── Flash ring (fallback when no sheet, matches Python reference) ─
            if (!has_sheet) {
                for (const auto& f : effects.flashes) {
                    double age = t - f.t0;
                    if (age < 0 || age > engine::FlashFX::DURATION) continue;
                    double p = age / engine::FlashFX::DURATION;
                    double r = f.radius_start + (f.radius_end - f.radius_start) * p;
                    uint8_t a = static_cast<uint8_t>(
                        std::clamp(255.0 * (1.0 - p) * double(intensity), 0.0, 255.0));
                    double fx_x = f.x, fx_y = f.y;
                    apply_expand_xy(fx_x, fx_y, W, H, expand);
                    draw_circle_outline(batch, respack.white_tex,
                                        fx_x, fx_y, r,
                                        f.color.r, f.color.g, f.color.b, a);
                }
            }
        }

        // ── Particles — additive blend for glow effect ──────────────────────
        if (show_particles && !respack.cfg.hide_particles) {
            batch.set_blend_mode(SDL_BLENDMODE_ADD);
            double now_ms = t * 1000.0;
            std::vector<engine::ParticleBurst::State> pts_buf;
            pts_buf.reserve(16);
            for (const auto& burst : effects.particles) {
                pts_buf.clear();
                burst.get_particles_inplace(now_ms, pts_buf);
                for (const auto& pt : pts_buf) {
                    uint8_t a_p = static_cast<uint8_t>(
                        std::clamp(static_cast<float>(pt.alpha) * intensity,
                                   0.0f, 255.0f));
                    if (a_p == 0) continue;
                    double pt_x = pt.x, pt_y = pt.y;
                    apply_expand_xy(pt_x, pt_y, W, H, expand);
                    batch.draw_rect(
                        pt_x - pt.size * 0.5, pt_y - pt.size * 0.5,
                        pt.size, pt.size,
                        burst.rgba.r, burst.rgba.g, burst.rgba.b, a_p);
                }
            }
            batch.set_blend_mode(SDL_BLENDMODE_BLEND);
        }
    }
};

} // namespace phigros::render
