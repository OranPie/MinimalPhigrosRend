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
    float top_align_offset = 0.0f;
    float aligned_digit_advance = 0.0f;
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

        float min_yoff = 0.0f;
        bool first = true;
        for (int i = 0; i < 96; ++i) {
            if (first || cdata[i].yoff < min_yoff) {
                min_yoff = cdata[i].yoff;
                first = false;
            }
        }
        top_align_offset = std::max(0.0f, -min_yoff);
        for (char ch = '0'; ch <= '9'; ++ch) {
            aligned_digit_advance = std::max(aligned_digit_advance, cdata[ch - 32].xadvance);
        }

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
    double font_scale = 1.0;
    bool font_align = true;
    bool overlay_transparent = false;

    bool init(SDL_Renderer* ren, const std::string& font_path, int w, int h,
              double font_scale_mul = 1.0, bool font_align_enabled = true,
              bool overlay_transparent_panels = false) {
        screen_w = w; screen_h = h;
        font_scale = std::max(0.5, std::min(3.0, font_scale_mul));
        font_align = font_align_enabled;
        overlay_transparent = overlay_transparent_panels;
        if (!font_path.empty()) {
            has_font = font_large.load(ren, font_path, static_cast<float>(36.0 * font_scale));
            if (has_font) font_small.load(ren, font_path, static_cast<float>(20.0 * font_scale));
        }
        return true;
    }

    float glyph_advance(const FontAtlas& font, char ch) const {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32 || uch >= 128) return 0.0f;
        const float advance = font.cdata[uch - 32].xadvance;
        if (font_align && ch >= '0' && ch <= '9' && font.aligned_digit_advance > 0.0f) {
            return font.aligned_digit_advance;
        }
        return advance;
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
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (uch < 32 || uch >= 128) continue;
            stbtt_bakedchar bc = font.cdata[uch - 32];

            SDL_Rect src;
            src.x = static_cast<int>(bc.x0);
            src.y = static_cast<int>(bc.y0);
            src.w = bc.x1 - bc.x0;
            src.h = bc.y1 - bc.y0;

            double dx = xpos + bc.xoff;
            double dy = ypos + font.top_align_offset + bc.yoff;
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
            xpos += glyph_advance(font, ch);
        }
    }

    double text_width(const FontAtlas& font, const std::string& text) const {
        if (!font.valid) return 0;
        float w = 0;
        float max_w = 0;
        for (char ch : text) {
            if (ch == '\n') { max_w = std::max(max_w, w); w = 0; continue; }
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (uch < 32 || uch >= 128) continue;
            w += glyph_advance(font, ch);
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
                const unsigned char uch = static_cast<unsigned char>(ch);
                if (uch < 32 || uch >= 128) continue;
                total_adv += glyph_advance(font, ch);
            }
            total_adv *= sx;

            float cursor = -total_adv * 0.5f;
            for (char ch : line) {
                const unsigned char uch = static_cast<unsigned char>(ch);
                if (uch < 32 || uch >= 128) continue;
                const stbtt_bakedchar& bc = font.cdata[uch - 32];

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

                cursor += glyph_advance(font, ch) * sx;
            }
        }
    }

    void draw(const SpriteBatch& batch, const hud::HudState& hud, double fps = 0) const {
        int W = screen_w, H = screen_h;
        const double panel_alpha = overlay_transparent ? 88.0 : 132.0;
        const double panel_pad = 10.0 * font_scale;
        const double panel_gap = 8.0 * font_scale;
        const double bar_h = std::max(4.0, 4.0 * font_scale);

        // Progress bar at top
        batch.draw_rect(0, 0, W, bar_h, 40, 40, 40, 180);
        batch.draw_rect(0, 0, W * hud.progress, bar_h, 230, 230, 230, 220);

        if (!has_font) {
            // Minimal fallback: just the progress bar
            return;
        }

        // Score (top right)
        {
            double tw = text_width(font_large, hud.score_text);
            double acc_tw = text_width(font_small, hud.acc_text);
            double px = W - std::max(tw, acc_tw) - panel_pad * 2.0 - 16.0;
            double py = bar_h + 8.0 * font_scale;
            double panel_h = text_line_height(font_large) + text_line_height(font_small) + panel_pad * 2.0;
            batch.draw_rect(px, py, std::max(tw, acc_tw) + panel_pad * 2.0, panel_h,
                            0, 0, 0, static_cast<uint8_t>(panel_alpha));
            draw_text(batch, font_large, hud.score_text,
                      px + panel_pad, py + panel_pad * 0.6, 255, 255, 255, 230);
            draw_text(batch, font_small, hud.acc_text,
                      px + panel_pad, py + panel_pad * 0.6 + text_line_height(font_large),
                      200, 200, 200, 200);
        }

        // Compact stats panel (top-left): Score / Acc / Combo
        {
            char combo_line[64];
            std::snprintf(combo_line, sizeof(combo_line), "COMBO %d/%d", hud.combo, hud.max_combo);
            std::string s_score = "SCORE " + hud.score_text;
            std::string s_acc   = "ACC   " + hud.acc_text;
            std::string s_combo = combo_line;

            const double x = 16.0;
            const double y0 = bar_h + 8.0 * font_scale;
            double tw = std::max({text_width(font_small, s_score),
                                  text_width(font_small, s_acc),
                                  text_width(font_small, s_combo)});
            double row_h = text_line_height(font_small) + 2.0 * font_scale;
            batch.draw_rect(x - panel_pad, y0 - panel_pad * 0.45,
                            tw + panel_pad * 2.0, row_h * 3.0 + panel_pad,
                            0, 0, 0, static_cast<uint8_t>(panel_alpha));
            draw_text(batch, font_small, s_score, x, y0 + 0.0, 235, 235, 235, 210);
            draw_text(batch, font_small, s_acc,   x, y0 + row_h, 210, 210, 210, 200);
            draw_text(batch, font_small, s_combo, x, y0 + row_h * 2.0, 210, 210, 210, 200);
        }

        // Combo (top center)
        if (hud.show_combo) {
            char combo_str[32];
            std::snprintf(combo_str, sizeof(combo_str), "%d", hud.combo);
            std::string combo_text = combo_str;
            double tw = text_width(font_large, combo_text);
            std::string label = "COMBO";
            double lw = text_width(font_small, label);
            double panel_w = std::max(tw, lw) + panel_pad * 2.0;
            double px = (W - panel_w) * 0.5;
            double py = bar_h + 8.0 * font_scale;
            double panel_h = text_line_height(font_large) + text_line_height(font_small) + panel_pad * 1.8;
            batch.draw_rect(px, py, panel_w, panel_h, 0, 0, 0, static_cast<uint8_t>(panel_alpha));
            draw_text(batch, font_large, combo_text,
                      px + (panel_w - tw) * 0.5, py + panel_pad * 0.4, 255, 255, 255, 240);
            draw_text(batch, font_small, label,
                      px + (panel_w - lw) * 0.5,
                      py + panel_pad * 0.4 + text_line_height(font_large) + panel_gap * 0.2,
                      200, 200, 200, 180);
        }

        // FPS (bottom left, debug)
        if (fps > 0) {
            char fps_str[32];
            std::snprintf(fps_str, sizeof(fps_str), "%.0f FPS", fps);
            draw_text(batch, font_small, fps_str, 8, H - (28.0 * font_scale), 150, 150, 150, 160);
        }
    }

    void destroy() { font_large.destroy(); font_small.destroy(); }
};

} // namespace phigros::render
