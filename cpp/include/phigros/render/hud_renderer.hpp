#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/texture.hpp"
#include "phigros/hud/hud.hpp"
#include "phigros/app/sdl_compat.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>

// Forward-declare stb_truetype
extern "C" {
    // Not needed as extern C, just include the header
}
#include "stb_truetype.h"

namespace phigros::render {

struct FontAtlas {
    Texture atlas_tex;
    stbtt_bakedchar cdata[128]; // ASCII only
    int atlas_w = 512, atlas_h = 512;
    float font_size = 32.0f;
    bool valid = false;

    bool load(SDL_Renderer* ren, const std::string& font_path, float size) {
        font_size = size;

        // Read font file
        FILE* f = fopen(font_path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> font_data(fsize);
        fread(font_data.data(), 1, fsize, f);
        fclose(f);

        // Choose atlas size based on font size
        atlas_w = atlas_h = (size > 48.0f) ? 1024 : 512;
        std::vector<uint8_t> atlas_pixels(atlas_w * atlas_h);
        int ret = stbtt_BakeFontBitmap(font_data.data(), 0, size,
            atlas_pixels.data(), atlas_w, atlas_h, 32, 96, cdata);
        if (ret <= 0) return false;

        // Convert grayscale to RGBA
        std::vector<uint8_t> rgba(atlas_w * atlas_h * 4);
        for (int i = 0; i < atlas_w * atlas_h; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = atlas_pixels[i];
        }
        atlas_tex = Texture::from_rgba(ren, rgba.data(), atlas_w, atlas_h);
        valid = atlas_tex.valid();
        return valid;
    }

    void destroy() { atlas_tex.destroy(); valid = false; }
};

struct HudRenderer {
    FontAtlas font_large;  // score, combo
    FontAtlas font_small;  // progress, accuracy

    bool has_font = false;
    int screen_w = 0, screen_h = 0;

    bool init(SDL_Renderer* ren, const std::string& font_path, int w, int h) {
        screen_w = w; screen_h = h;
        if (!font_path.empty()) {
            has_font = font_large.load(ren, font_path, 36.0f);
            if (has_font) font_small.load(ren, font_path, 20.0f);
        }
        return true;
    }

    void draw_text(const SpriteBatch& batch, const FontAtlas& font,
                   const std::string& text, double x, double y,
                   uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                   uint8_t a = 255) const {
        if (!font.valid) return;

        const float origin_x = static_cast<float>(x);
        float xpos = origin_x;
        float ypos = static_cast<float>(y);

        for (char ch : text) {
            if (ch == '\n') {
                xpos = origin_x;
                ypos += font.font_size;
                continue;
            }
            if (ch < 32 || ch >= 128) continue;
            stbtt_bakedchar bc = font.cdata[ch - 32];

            SDL_Rect src;
            src.x = static_cast<int>(bc.x0);
            src.y = static_cast<int>(bc.y0);
            src.w = bc.x1 - bc.x0;
            src.h = bc.y1 - bc.y0;

            double dx = xpos + bc.xoff;
            double dy = ypos + bc.yoff;
            double dw = src.w;
            double dh = src.h;

            font.atlas_tex.set_color_mod(r, g, b);
            font.atlas_tex.set_alpha_mod(a);

            SDL_FRect dst;
            dst.x = static_cast<float>(dx);
            dst.y = static_cast<float>(dy);
            dst.w = static_cast<float>(dw);
            dst.h = static_cast<float>(dh);

            app::sdl::render_copy(batch.ren, font.atlas_tex.tex, &src, &dst);
            xpos += bc.xadvance;
        }
    }

    double text_width(const FontAtlas& font, const std::string& text) const {
        if (!font.valid) return 0;
        float w = 0;
        float max_w = 0;
        for (char ch : text) {
            if (ch == '\n') { max_w = std::max(max_w, w); w = 0; continue; }
            if (ch < 32 || ch >= 128) continue;
            w += font.cdata[ch - 32].xadvance;
        }
        return std::max(max_w, w);
    }

    /// Return the line height of the font (same spacing used by draw_text for \n).
    double text_line_height(const FontAtlas& font) const {
        return font.valid ? static_cast<double>(font.font_size) : 0.0;
    }

    // Draw text centered at (cx, cy), rotated by rot_rad, scaled by sx/sy.
    // Goes through SpriteBatch so it works with DrawList recording (motion blur, trail).
    // Handles \n newlines (v153+): each line is offset perpendicular to the baseline.
    void draw_text_rotated(const SpriteBatch& batch, const FontAtlas& font,
                           const std::string& text,
                           double cx, double cy, double rot_rad,
                           float sx, float sy,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
        if (!font.valid || text.empty()) return;

        // Split on \n
        std::vector<std::string_view> lines_sv;
        std::string_view sv(text);
        size_t pos = 0, found;
        while ((found = sv.find('\n', pos)) != std::string_view::npos) {
            lines_sv.push_back(sv.substr(pos, found - pos));
            pos = found + 1;
        }
        lines_sv.push_back(sv.substr(pos));

        const float cos_r = static_cast<float>(std::cos(rot_rad));
        const float sin_r = static_cast<float>(std::sin(rot_rad));
        const float line_h = font.font_size * sy;
        const int n = static_cast<int>(lines_sv.size());

        for (int li = 0; li < n; ++li) {
            const std::string_view& line = lines_sv[li];
            if (line.empty()) continue;

            // Vertical offset for this line (centered across all lines)
            float vy = (li - (n - 1) * 0.5f) * line_h;
            // Perpendicular direction (rotated 90° from baseline)
            double lx_off = -sin_r * vy;
            double ly_off =  cos_r * vy;

            // Total advance width for centering this line
            float total_adv = 0;
            for (char ch : line) {
                if (ch < 32 || ch >= 128) continue;
                total_adv += font.cdata[ch - 32].xadvance;
            }
            total_adv *= sx;

            float cursor = -total_adv * 0.5f;
            for (char ch : line) {
                if (ch < 32 || ch >= 128) continue;
                const stbtt_bakedchar& bc = font.cdata[ch - 32];

                float gw = static_cast<float>(bc.x1 - bc.x0) * sx;
                float gh = static_cast<float>(bc.y1 - bc.y0) * sy;

                // Glyph center in local (unrotated) space
                float lx = cursor + bc.xoff * sx + gw * 0.5f;
                float ly = bc.yoff * sy + gh * 0.5f;

                // Rotate into world space and apply line offset
                double wx = cx + lx_off + cos_r * lx - sin_r * ly;
                double wy = cy + ly_off + sin_r * lx + cos_r * ly;

                SDL_Rect src{ bc.x0, bc.y0, bc.x1 - bc.x0, bc.y1 - bc.y0 };
                batch.draw_texture(font.atlas_tex, wx, wy, gw, gh,
                                   rot_rad, r, g, b, a, &src);

                cursor += bc.xadvance * sx;
            }
        }
    }

    void draw(const SpriteBatch& batch, const hud::HudState& hud, double fps = 0) const {
        int W = screen_w, H = screen_h;

        // Progress bar at top
        double bar_h = 4.0;
        batch.draw_rect(0, 0, W, bar_h, 40, 40, 40, 180);
        batch.draw_rect(0, 0, W * hud.progress, bar_h, 230, 230, 230, 220);

        if (!has_font) {
            // Minimal fallback: just the progress bar
            return;
        }

        // Score (top right)
        {
            double tw = text_width(font_large, hud.score_text);
            draw_text(batch, font_large, hud.score_text,
                      W - tw - 16, bar_h + 8, 255, 255, 255, 230);
        }

        // Accuracy (below score)
        {
            double tw = text_width(font_small, hud.acc_text);
            draw_text(batch, font_small, hud.acc_text,
                      W - tw - 16, bar_h + 48, 200, 200, 200, 200);
        }

        // Compact stats panel (top-left): Score / Acc / Combo
        {
            char combo_line[64];
            std::snprintf(combo_line, sizeof(combo_line), "COMBO %d/%d", hud.combo, hud.max_combo);
            std::string s_score = "SCORE " + hud.score_text;
            std::string s_acc   = "ACC   " + hud.acc_text;
            std::string s_combo = combo_line;

            const double x = 16.0;
            const double y0 = bar_h + 10.0;
            draw_text(batch, font_small, s_score, x, y0 + 0.0, 235, 235, 235, 210);
            draw_text(batch, font_small, s_acc,   x, y0 + 22.0, 210, 210, 210, 200);
            draw_text(batch, font_small, s_combo, x, y0 + 44.0, 210, 210, 210, 200);
        }

        // Combo (top center)
        if (hud.show_combo) {
            char combo_str[32];
            std::snprintf(combo_str, sizeof(combo_str), "%d", hud.combo);
            std::string combo_text = combo_str;
            double tw = text_width(font_large, combo_text);
            draw_text(batch, font_large, combo_text,
                      (W - tw) * 0.5, bar_h + 8, 255, 255, 255, 240);

            std::string label = "COMBO";
            double lw = text_width(font_small, label);
            draw_text(batch, font_small, label,
                      (W - lw) * 0.5, bar_h + 46, 200, 200, 200, 180);
        }

        // FPS (bottom left, debug)
        if (fps > 0) {
            char fps_str[32];
            std::snprintf(fps_str, sizeof(fps_str), "%.0f FPS", fps);
            draw_text(batch, font_small, fps_str, 8, H - 28, 150, 150, 150, 160);
        }
    }

    void destroy() { font_large.destroy(); font_small.destroy(); }
};

} // namespace phigros::render
