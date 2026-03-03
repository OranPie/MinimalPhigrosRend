#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/io/respack.hpp"
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace phigros::render {

struct NoteRenderer {
    double base_note_w = 0.0;  // 0.06 * W
    double base_note_h = 0.0;  // 0.018 * H
    double note_scale_x = 2.5;
    double note_scale_y = 1.0;
    double miss_fade_sec = 0.5;
    double bad_ghost_sec = 0.3;

    void init(int W, int H, double scale_x, double scale_y) {
        base_note_w = 0.06 * W;
        base_note_h = 0.018 * H;
        note_scale_x = scale_x;
        note_scale_y = scale_y;
    }

    void draw(const SpriteBatch& batch,
              const io::Respack& respack,
              const std::vector<NoteSnapshot>& notes,
              double t) const {

        for (auto& ns : notes) {
            // Skip holds (drawn by HoldRenderer)
            if (ns.is_hold) continue;

            const auto& tex = respack.note_texture(ns.kind, ns.mh);
            if (!tex.valid()) continue;

            double ws = base_note_w * note_scale_x;
            double hs = base_note_h * note_scale_y;

            // Color and alpha
            uint8_t r = ns.color.r, g = ns.color.g, b = ns.color.b;
            double alpha_f = ns.alpha * 255.0;

            // Miss dimming: desaturate over time
            if (ns.miss) {
                double gray = 0.3 * r + 0.59 * g + 0.11 * b;
                r = static_cast<uint8_t>(gray);
                g = static_cast<uint8_t>(gray);
                b = static_cast<uint8_t>(gray);
                alpha_f *= 0.5;
            }

            uint8_t a = static_cast<uint8_t>(std::clamp(alpha_f, 0.0, 255.0));
            if (a == 0) continue;

            // Rotate note to match line angle
            batch.draw_texture(tex, ns.wx, ns.wy, ws, hs, ns.line_rot, r, g, b, a);
        }
    }
};

} // namespace phigros::render
