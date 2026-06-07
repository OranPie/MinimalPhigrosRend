#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/texture.hpp"
#include "phigros/hud/hud.hpp"
#include "phigros/app/sdl_compat.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>

// Forward-declare stb_truetype
extern "C" {
    // Not needed as extern C, just include the header
}
#include "stb_truetype.h"

namespace phigros::render {

struct FontAtlas {
    Texture atlas_tex;
    stbtt_bakedchar cdata[128]; // Fast baked path for ASCII.
    struct GlyphTexture {
        Texture tex;
        int w = 0;
        int h = 0;
        float xoff = 0.0f;
        float yoff = 0.0f;
        float advance = 0.0f;
    };
    int atlas_w = 512, atlas_h = 512;
    float font_size = 32.0f;
    float top_align_offset = 0.0f;
    float line_height = 32.0f;
    float aligned_digit_advance = 0.0f;
    bool valid = false;
    SDL_Renderer* renderer = nullptr;
    std::vector<uint8_t> font_data;
    stbtt_fontinfo font_info{};
    float font_scale = 1.0f;
    mutable std::unordered_map<uint32_t, GlyphTexture> glyph_cache;

    bool load(SDL_Renderer* ren, const std::string& font_path, float size) {
        font_size = size;
        line_height = size;
        renderer = ren;
        glyph_cache.clear();

        // Read font file
        FILE* f = fopen(font_path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        font_data.resize(static_cast<size_t>(std::max<long>(0, fsize)));
        fread(font_data.data(), 1, fsize, f);
        fclose(f);
        if (font_data.empty()) return false;

        const int offset = stbtt_GetFontOffsetForIndex(font_data.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&font_info, font_data.data(), offset)) return false;
        font_scale = stbtt_ScaleForPixelHeight(&font_info, size);
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &gap);
        top_align_offset = std::max(0.0f, ascent * font_scale);
        line_height = std::max(size, (ascent - descent + gap) * font_scale);

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
        top_align_offset = std::max(top_align_offset, ascent * font_scale);
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

    float glyph_advance(uint32_t cp, bool align_digits) const {
        if (cp >= 32 && cp < 128) {
            const float advance = cdata[cp - 32].xadvance;
            if (align_digits && cp >= '0' && cp <= '9' && aligned_digit_advance > 0.0f)
                return aligned_digit_advance;
            return advance;
        }
        int advance = 0;
        int lsb = 0;
        stbtt_GetCodepointHMetrics(&font_info, static_cast<int>(cp), &advance, &lsb);
        return std::max(1.0f, advance * font_scale);
    }

    const GlyphTexture* glyph(uint32_t cp) const {
        auto it = glyph_cache.find(cp);
        if (it != glyph_cache.end()) return &it->second;
        auto inserted = glyph_cache.emplace(cp, GlyphTexture{});
        GlyphTexture& g = inserted.first->second;
        g.advance = glyph_advance(cp, false);
        if (!renderer || font_data.empty()) return &g;

        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            &font_info, 0.0f, font_scale, static_cast<int>(cp), &w, &h, &xoff, &yoff);
        if (!bitmap || w <= 0 || h <= 0) {
            if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
            return &g;
        }

        std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
        for (int i = 0; i < w * h; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = bitmap[i];
        }
        g.tex = Texture::from_rgba(renderer, rgba.data(), w, h);
        g.w = w;
        g.h = h;
        g.xoff = static_cast<float>(xoff);
        g.yoff = static_cast<float>(yoff);
        stbtt_FreeBitmap(bitmap, nullptr);
        return &g;
    }

    void destroy() {
        atlas_tex.destroy();
        for (auto& item : glyph_cache) item.second.tex.destroy();
        glyph_cache.clear();
        font_data.clear();
        renderer = nullptr;
        valid = false;
    }
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

    static uint32_t next_codepoint(std::string_view text, size_t& pos) {
        if (pos >= text.size()) return 0;
        const unsigned char c0 = static_cast<unsigned char>(text[pos++]);
        if (c0 < 0x80) return c0;
        auto continuation = [&](uint32_t& out) -> bool {
            if (pos >= text.size()) return false;
            const unsigned char c = static_cast<unsigned char>(text[pos]);
            if ((c & 0xc0) != 0x80) return false;
            out = (out << 6) | (c & 0x3f);
            ++pos;
            return true;
        };
        if ((c0 & 0xe0) == 0xc0) {
            uint32_t cp = c0 & 0x1f;
            return continuation(cp) ? cp : '?';
        }
        if ((c0 & 0xf0) == 0xe0) {
            uint32_t cp = c0 & 0x0f;
            return continuation(cp) && continuation(cp) ? cp : '?';
        }
        if ((c0 & 0xf8) == 0xf0) {
            uint32_t cp = c0 & 0x07;
            return continuation(cp) && continuation(cp) && continuation(cp) ? cp : '?';
        }
        return '?';
    }

    float glyph_advance(const FontAtlas& font, uint32_t cp) const {
        return font.glyph_advance(cp, font_align);
    }

    void draw_text(const SpriteBatch& batch, const FontAtlas& font,
                   const std::string& text, double x, double y,
                   uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                   uint8_t a = 255) const {
        if (!font.valid) return;

        const float origin_x = static_cast<float>(x);
        float xpos = origin_x;
        float ypos = static_cast<float>(y);

        size_t pos = 0;
        while (pos < text.size()) {
            const uint32_t cp = next_codepoint(text, pos);
            if (cp == '\n') {
                xpos = origin_x;
                ypos += font.line_height;
                continue;
            }
            if (cp < 32) continue;
            if (cp < 128) {
                const stbtt_bakedchar& bc = font.cdata[cp - 32];
                SDL_Rect src;
                src.x = static_cast<int>(bc.x0);
                src.y = static_cast<int>(bc.y0);
                src.w = bc.x1 - bc.x0;
                src.h = bc.y1 - bc.y0;

                SDL_FRect dst;
                dst.x = xpos + bc.xoff;
                dst.y = ypos + font.top_align_offset + bc.yoff;
                dst.w = static_cast<float>(src.w);
                dst.h = static_cast<float>(src.h);

                font.atlas_tex.set_color_mod(r, g, b);
                font.atlas_tex.set_alpha_mod(a);
                app::sdl::render_copy(batch.ren, font.atlas_tex.tex, &src, &dst);
            } else if (const auto* glyph = font.glyph(cp)) {
                if (glyph->tex.valid() && glyph->w > 0 && glyph->h > 0) {
                    glyph->tex.set_color_mod(r, g, b);
                    glyph->tex.set_alpha_mod(a);
                    SDL_FRect dst;
                    dst.x = xpos + glyph->xoff;
                    dst.y = ypos + font.top_align_offset + glyph->yoff;
                    dst.w = static_cast<float>(glyph->w);
                    dst.h = static_cast<float>(glyph->h);
                    app::sdl::render_copy(batch.ren, glyph->tex.tex, nullptr, &dst);
                }
            }
            xpos += glyph_advance(font, cp);
        }
    }

    double text_width(const FontAtlas& font, const std::string& text) const {
        if (!font.valid) return 0;
        float w = 0;
        float max_w = 0;
        size_t pos = 0;
        while (pos < text.size()) {
            const uint32_t cp = next_codepoint(text, pos);
            if (cp == '\n') { max_w = std::max(max_w, w); w = 0; continue; }
            if (cp < 32) continue;
            w += glyph_advance(font, cp);
        }
        return std::max(max_w, w);
    }

    /// Return the line height of the font (same spacing used by draw_text for \n).
    double text_line_height(const FontAtlas& font) const {
        return font.valid ? static_cast<double>(font.line_height) : 0.0;
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
            size_t width_pos = 0;
            while (width_pos < line.size()) {
                const uint32_t cp = next_codepoint(line, width_pos);
                if (cp < 32) continue;
                total_adv += glyph_advance(font, cp);
            }
            total_adv *= sx;

            float cursor = -total_adv * 0.5f;
            size_t glyph_pos = 0;
            while (glyph_pos < line.size()) {
                const uint32_t cp = next_codepoint(line, glyph_pos);
                if (cp < 32) continue;
                if (cp < 128) {
                    const stbtt_bakedchar& bc = font.cdata[cp - 32];
                    float gw = static_cast<float>(bc.x1 - bc.x0) * sx;
                    float gh = static_cast<float>(bc.y1 - bc.y0) * sy;
                    float lx = cursor + bc.xoff * sx + gw * 0.5f;
                    float ly = bc.yoff * sy + gh * 0.5f;
                    double wx = cx + lx_off + cos_r * lx - sin_r * ly;
                    double wy = cy + ly_off + sin_r * lx + cos_r * ly;
                    SDL_Rect src{ bc.x0, bc.y0, bc.x1 - bc.x0, bc.y1 - bc.y0 };
                    batch.draw_texture(font.atlas_tex, wx, wy, gw, gh,
                                       rot_rad, r, g, b, a, &src);
                } else if (const auto* glyph = font.glyph(cp)) {
                    if (glyph->tex.valid() && glyph->w > 0 && glyph->h > 0) {
                        float gw = static_cast<float>(glyph->w) * sx;
                        float gh = static_cast<float>(glyph->h) * sy;
                        float lx = cursor + glyph->xoff * sx + gw * 0.5f;
                        float ly = glyph->yoff * sy + gh * 0.5f;
                        double wx = cx + lx_off + cos_r * lx - sin_r * ly;
                        double wy = cy + ly_off + sin_r * lx + cos_r * ly;
                        batch.draw_texture(glyph->tex, wx, wy, gw, gh,
                                           rot_rad, r, g, b, a, nullptr);
                    }
                }
                cursor += glyph_advance(font, cp) * sx;
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
