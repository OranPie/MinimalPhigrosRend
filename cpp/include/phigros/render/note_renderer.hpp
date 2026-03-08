#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"   // apply_expand_xy
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
    bool note_outline = false;  // draw dark outline at 1.08× size before the note

    void init(int W, int H, double scale_x, double scale_y) {
        base_note_w = 0.06 * W;
        base_note_h = 0.018 * H;
        note_scale_x = scale_x;
        note_scale_y = scale_y;
    }

    void draw(const SpriteBatch& batch,
              const io::Respack& respack,
              const std::vector<NoteSnapshot>& notes,
              double t,
              int W = 1280, int H = 720, double expand = 1.0) const {

        for (auto& ns : notes) {
            // Skip holds (drawn by HoldRenderer)
            if (ns.is_hold) continue;

            const auto& tex = respack.note_texture(ns.kind, ns.mh);
            if (!tex.valid()) continue;

            // Keep note width in chart-space, but derive height from actual
            // texture aspect ratio so tap/drag/flick can have different shapes
            // across resource packs.
            double ws = base_note_w * note_scale_x * ns.size_px;
            double hs = base_note_h * note_scale_y * ns.size_px;
            if (tex.w > 0 && tex.h > 0) {
                hs = ws * (static_cast<double>(tex.h) / static_cast<double>(tex.w)) * note_scale_y;
            }

            // Apply expand: compress world coords toward screen centre (matches Python)
            double draw_x = ns.wx, draw_y = ns.wy;
            apply_expand_xy(draw_x, draw_y, W, H, expand);

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

            // Outline: draw at 1.08× size with dark colour before main sprite
            if (note_outline && !ns.miss) {
                uint8_t oa = static_cast<uint8_t>(a * 0.5);
                double draw_angle = ns.line_rot + ns.skew * (M_PI / 180.0);
                if (oa > 0)
                    batch.draw_texture(tex, draw_x, draw_y, ws * 1.08, hs * 1.08,
                                       draw_angle, 0, 0, 0, oa);
            }

            // Rotate note to match line angle, plus skewControl offset
            double draw_angle = ns.line_rot + ns.skew * (M_PI / 180.0);
            batch.draw_texture(tex, draw_x, draw_y, ws, hs, draw_angle, r, g, b, a);
        }
    }
};

} // namespace phigros::render
