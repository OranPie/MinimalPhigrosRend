#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/io/respack.hpp"
#include <cmath>
#include <vector>

namespace phigros::render {

struct LineRenderer {
    double line_w = 4.0;  // line thickness in pixels
    double dot_r  = 5.0;  // center dot radius

    void draw(const SpriteBatch& batch, const Texture& white_tex,
              const std::vector<LineSnapshot>& lines,
              int W, int H, double expand) const {
        double half_len = W * expand * 0.5;

        for (auto& ls : lines) {
            if (ls.alpha01 <= 0.001) continue;

            uint8_t a = static_cast<uint8_t>(ls.alpha01 * 255);
            uint8_t r = ls.color.r, g = ls.color.g, b = ls.color.b;

            double tx = ls.cos_rot;
            double ty = ls.sin_rot;

            double x0 = ls.x - tx * half_len;
            double y0 = ls.y - ty * half_len;
            double x1 = ls.x + tx * half_len;
            double y1 = ls.y + ty * half_len;

            // Draw line as a rotated rectangle for consistent thickness
            batch.draw_rotated_rect(white_tex,
                ls.x, ls.y, half_len * 2.0, line_w,
                ls.rot, r, g, b, a);

            // Center dot
            if (dot_r > 1.0) {
                double dot_size = dot_r * 2.0;
                batch.draw_rotated_rect(white_tex,
                    ls.x, ls.y, dot_size, dot_size,
                    0.0, r, g, b, static_cast<uint8_t>(std::min(220.0 * ls.alpha01, 255.0)));
            }
        }
    }
};

} // namespace phigros::render
