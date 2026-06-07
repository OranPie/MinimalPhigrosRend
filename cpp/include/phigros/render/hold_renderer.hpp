#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"   // apply_expand_xy
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
              double t,
              int W = 1280, int H = 720, double expand = 1.0) const {
        for (auto& ns : notes) {
            if (!ns.is_hold) continue;

            const bool use_mh_variant = ns.mh && respack.has_hold_mh_variant();
            const auto& tex = use_mh_variant ? respack.hold_mh : respack.hold;
            if (!tex.valid()) continue;

            double ws = base_note_w * note_scale_x * ns.size_px;
            ws = apply_expand_size(ws, expand);

            // Head and tail positions from snapshot — apply expand transform
            double hx = ns.wx, hy = ns.wy;
            double tx = ns.wx_tail, ty = ns.wy_tail;
            apply_expand_xy(hx, hy, W, H, expand);
            apply_expand_xy(tx, ty, W, H, expand);

            // Direction vector from tail to head
            double dx = hx - tx, dy = hy - ty;
            double total_len = std::sqrt(dx * dx + dy * dy);
            if (total_len < 1.0) continue;

            double angle = std::atan2(dy, dx) - M_PI * 0.5; // perpendicular to direction

            // Atlas dimensions
            int head_h = use_mh_variant ? respack.cfg.hold_head_h_mh : respack.cfg.hold_head_h;
            int tail_h = use_mh_variant ? respack.cfg.hold_tail_h_mh : respack.cfg.hold_tail_h;
            head_h = std::clamp(head_h, 0, tex.h);
            tail_h = std::clamp(tail_h, 0, std::max(0, tex.h - head_h));
            int body_h = tex.h - head_h - tail_h;
            if (body_h <= 0) body_h = 1;

            const bool draw_head = ns.draw_hold_head || respack.cfg.hold_keep_head;

            // Scale factors: map texture pixels to screen pixels
            double px_per_texel = ws / tex.w;
            double head_screen_h = head_h * px_per_texel;
            double tail_screen_h = tail_h * px_per_texel;
            double body_screen_h = respack.cfg.hold_compact
                ? total_len
                : std::max(0.0, total_len - tail_screen_h - (draw_head ? head_screen_h : 0.0));

            // Color and alpha
            uint8_t r = ns.color.r, g = ns.color.g, b = ns.color.b;
            double alpha_f = ns.alpha * 255.0;
            // Failed holds stay visible until their tail crosses the line, but
            // should render as a miss whether the head was hit or fully missed.
            if (ns.miss || ns.hold_hit_failed) { alpha_f *= 0.5; r = g = b = 128; }
            uint8_t a = static_cast<uint8_t>(std::clamp(alpha_f, 0.0, 255.0));
            if (a == 0) continue;

            // Direction unit vector (tail → head)
            double ux = dx / total_len, uy = dy / total_len;

            // Draw 3 slices along the direction vector

            // 1. Tail (at tail end)
            if (tail_h > 0 && tail_screen_h > 0.5) {
                double tail_offset = respack.cfg.hold_compact ? 0.0 : tail_screen_h * 0.5;
                double cx = tx + ux * tail_offset;
                double cy = ty + uy * tail_offset;
                batch.draw_texture_region(
                    tex, 0, 0, tex.w, tail_h,
                    cx, cy, ws, tail_screen_h, angle, r, g, b, a);
            }

            // 2. Body (between tail and head)
            if (body_screen_h > 0.5) {
                const int body_y = tail_h;
                const double body_start = respack.cfg.hold_compact ? 0.0 : tail_screen_h;
                if (respack.cfg.hold_repeat) {
                    const double tile_h = std::max(1.0, body_h * px_per_texel);
                    double drawn = 0.0;
                    while (drawn < body_screen_h - 0.5) {
                        double seg_h = std::min(tile_h, body_screen_h - drawn);
                        double src_h_f = (seg_h / tile_h) * body_h;
                        int src_h = std::clamp(static_cast<int>(std::ceil(src_h_f)), 1, body_h);
                        double cx = tx + ux * (body_start + drawn + seg_h * 0.5);
                        double cy = ty + uy * (body_start + drawn + seg_h * 0.5);
                        batch.draw_texture_region(
                            tex, 0, body_y, tex.w, src_h,
                            cx, cy, ws, seg_h, angle, r, g, b, a);
                        drawn += seg_h;
                    }
                } else {
                    double cx = tx + ux * (body_start + body_screen_h * 0.5);
                    double cy = ty + uy * (body_start + body_screen_h * 0.5);
                    batch.draw_texture_region(
                        tex, 0, body_y, tex.w, body_h,
                        cx, cy, ws, body_screen_h, angle, r, g, b, a);
                }
            }

            // 3. Head (at head end) — hidden while holding unless respack holdKeepHead=true
            if (draw_head && head_h > 0 && head_screen_h > 0.5)
            {
                double head_offset = respack.cfg.hold_compact ? 0.0 : head_screen_h * 0.5;
                double cx = hx - ux * head_offset;
                double cy = hy - uy * head_offset;
                batch.draw_texture_region(
                    tex, 0, tail_h + body_h, tex.w, head_h,
                    cx, cy, ws, head_screen_h, angle, r, g, b, a);
            }
        }
    }
};

} // namespace phigros::render
