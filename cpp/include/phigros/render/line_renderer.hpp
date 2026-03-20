#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"   // apply_expand_xy
#include "phigros/render/texture.hpp"
#include "phigros/io/respack.hpp"
#include <cmath>
#include <functional>
#include <vector>

namespace phigros::render {

struct LineRenderer {
    double line_w = 4.0;  // line thickness in pixels
    double dot_r  = 5.0;  // center dot radius

    // Optional callback: returns a Texture* for a given path, or nullptr if not loaded.
    // Used for custom line textures (RPE Texture field).
    std::function<const Texture*(const std::string&)> texture_lookup;

    // Optional callback: draw text at (x, y) with given color and alpha.
    // Used for RPE textEvents. Color comes from the line's colorEvents (or white if none).
    std::function<void(const std::string& text, double x, double y,
                       double rot, float sx, float sy,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a)> text_draw;

    // Draw non-cover lines (is_cover == false). Call before notes.
    void draw(const SpriteBatch& batch, const Texture& white_tex,
              const std::vector<LineSnapshot>& lines,
              int W, int H, double expand, bool cover_pass = false) const {
        double half_len = W * 0.5;

        for (auto& ls : lines) {
            if (ls.is_cover != cover_pass) continue;
            if (ls.alpha01 <= 0.001) continue;

            uint8_t a = static_cast<uint8_t>(ls.alpha01 * 255);
            uint8_t r = ls.color.r, g = ls.color.g, b = ls.color.b;

            double cx = ls.x, cy = ls.y;
            apply_expand_xy(cx, cy, W, H, expand);

            // Text event: draw text instead of line/texture (text clears texture per RPE spec)
            if (!ls.text.empty()) {
                if (text_draw)
                    text_draw(ls.text, cx, cy, ls.rot, ls.scale_x, ls.scale_y, r, g, b, a);
                continue;
            }

            // Custom texture
            if (ls.texture_path && !ls.texture_path->empty() && texture_lookup) {
                const Texture* tex = texture_lookup(*ls.texture_path);
                if (tex && tex->valid()) {
                    double tw = apply_expand_size(tex->w * ls.scale_x, expand);
                    double th = apply_expand_size(tex->h * ls.scale_y, expand);
                    batch.draw_texture(*tex, cx, cy, tw, th, ls.rot, r, g, b, a);
                    continue;
                }
            }

            // Default: white line rect
            batch.draw_rotated_rect(white_tex,
                cx, cy,
                apply_expand_size(half_len * 2.0 * ls.scale_x, expand),
                apply_expand_size(line_w * ls.scale_y, expand),
                ls.rot, r, g, b, a);

            // Center dot
            if (dot_r > 1.0) {
                double dot_size = apply_expand_size(dot_r * 2.0, expand);
                batch.draw_rotated_rect(white_tex,
                    cx, cy, dot_size, dot_size,
                    0.0, r, g, b, static_cast<uint8_t>(std::min(220.0 * ls.alpha01, 255.0)));
            }
        }
    }
};

} // namespace phigros::render
