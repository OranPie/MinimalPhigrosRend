#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/texture.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/io/respack.hpp"
#include <cmath>
#include <algorithm>

namespace phigros::render {

struct HitFXRenderer {

    void draw(const SpriteBatch& batch,
              const io::Respack& respack,
              const engine::EffectManager& effects,
              double t) const {

        int cols = respack.cfg.hitfx_cols;
        int rows = respack.cfg.hitfx_rows;
        int total_frames = cols * rows;
        double duration = respack.cfg.hitfx_duration;
        double scale = respack.cfg.hitfx_scale;

        const auto& sheet = respack.hitfx_sheet;
        if (!sheet.valid() || total_frames <= 0) return;

        int cell_w = sheet.w / cols;
        int cell_h = sheet.h / rows;
        if (cell_w <= 0 || cell_h <= 0) return;

        double draw_size = cell_w * scale;

        // Draw hit effects (spritesheet animation)
        for (auto& fx : effects.hitfx) {
            double age = t - fx.t0;
            if (age < 0 || age > duration) continue;

            double p = std::clamp(age / duration, 0.0, 0.999);
            int frame_idx = static_cast<int>(p * total_frames);
            frame_idx = std::clamp(frame_idx, 0, total_frames - 1);

            int ix = frame_idx % cols;
            int iy = frame_idx / cols;

            uint8_t cr = fx.rgba.r, cg = fx.rgba.g, cb = fx.rgba.b;
            uint8_t a = static_cast<uint8_t>(std::clamp(255.0 * (1.0 - age / duration), 0.0, 255.0));

            if (!respack.cfg.hitfx_tinted) { cr = 255; cg = 255; cb = 255; }

            batch.draw_texture_region(sheet,
                ix * cell_w, iy * cell_h, cell_w, cell_h,
                fx.x, fx.y, draw_size, draw_size,
                respack.cfg.hitfx_rotate ? fx.rot : 0.0,
                cr, cg, cb, a);
        }

        // Draw particles
        if (!respack.cfg.hide_particles) {
            double now_ms = t * 1000.0;
            for (auto& burst : effects.particles) {
                auto pts = burst.get_particles(now_ms);
                for (auto& pt : pts) {
                    uint8_t a_p = static_cast<uint8_t>(std::clamp(pt.alpha, 0, 255));
                    if (a_p == 0) continue;
                    batch.draw_rect(
                        pt.x - pt.size * 0.5, pt.y - pt.size * 0.5,
                        pt.size, pt.size,
                        burst.rgba.r, burst.rgba.g, burst.rgba.b, a_p);
                }
            }
        }
    }
};

} // namespace phigros::render
