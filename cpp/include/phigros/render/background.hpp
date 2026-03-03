#pragma once
#include "phigros/render/texture.hpp"
#include "phigros/render/sprite_batch.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

// Forward-declare stb_image
extern "C" {
    unsigned char* stbi_load(const char*, int*, int*, int*, int);
    void stbi_image_free(void*);
}

namespace phigros::render {

struct BackgroundRenderer {
    Texture bg_tex;
    int screen_w = 0, screen_h = 0;
    bool has_bg = false;

    void load(SDL_Renderer* ren, const std::string& path, int sw, int sh, int blur_factor) {
        screen_w = sw; screen_h = sh;
        if (path.empty()) return;

        // Load original image
        int iw, ih, ch;
        uint8_t* pixels = stbi_load(path.c_str(), &iw, &ih, &ch, 4);
        if (!pixels) return;

        // Cover-mode scaling: calculate crop region
        double scale = std::max(
            static_cast<double>(sw) / iw,
            static_cast<double>(sh) / ih);
        int scaled_w = static_cast<int>(iw * scale);
        int scaled_h = static_cast<int>(ih * scale);

        // Simple box blur: downscale → upscale
        if (blur_factor > 1) {
            int small_w = std::max(1, iw / blur_factor);
            int small_h = std::max(1, ih / blur_factor);
            std::vector<uint8_t> small(small_w * small_h * 4);

            // Bilinear downscale
            for (int y = 0; y < small_h; ++y) {
                for (int x = 0; x < small_w; ++x) {
                    double fx = static_cast<double>(x) * iw / small_w;
                    double fy = static_cast<double>(y) * ih / small_h;
                    int sx = std::min(static_cast<int>(fx), iw - 1);
                    int sy = std::min(static_cast<int>(fy), ih - 1);
                    int idx_src = (sy * iw + sx) * 4;
                    int idx_dst = (y * small_w + x) * 4;
                    small[idx_dst + 0] = pixels[idx_src + 0];
                    small[idx_dst + 1] = pixels[idx_src + 1];
                    small[idx_dst + 2] = pixels[idx_src + 2];
                    small[idx_dst + 3] = 255;
                }
            }
            stbi_image_free(pixels);

            // Upscale to screen size (nearest-neighbor; SDL will smooth on render)
            std::vector<uint8_t> upscaled(sw * sh * 4);
            for (int y = 0; y < sh; ++y) {
                for (int x = 0; x < sw; ++x) {
                    int sx = x * small_w / sw;
                    int sy = y * small_h / sh;
                    sx = std::min(sx, small_w - 1);
                    sy = std::min(sy, small_h - 1);
                    int idx_src = (sy * small_w + sx) * 4;
                    int idx_dst = (y * sw + x) * 4;
                    upscaled[idx_dst + 0] = small[idx_src + 0];
                    upscaled[idx_dst + 1] = small[idx_src + 1];
                    upscaled[idx_dst + 2] = small[idx_src + 2];
                    upscaled[idx_dst + 3] = 255;
                }
            }
            bg_tex = Texture::from_rgba(ren, upscaled.data(), sw, sh);
        } else {
            bg_tex = Texture::from_rgba(ren, pixels, iw, ih);
            stbi_image_free(pixels);
        }
        has_bg = bg_tex.valid();
    }

    void draw(const SpriteBatch& batch, int dim_alpha = 120) const {
        if (has_bg) {
            batch.draw_texture(bg_tex,
                screen_w * 0.5, screen_h * 0.5,
                static_cast<double>(screen_w), static_cast<double>(screen_h));
        }
        // Dim overlay
        if (dim_alpha > 0) {
            batch.draw_rect(0, 0, screen_w, screen_h, 0, 0, 0,
                            static_cast<uint8_t>(std::min(dim_alpha, 255)));
        }
    }

    void destroy() { bg_tex.destroy(); has_bg = false; }
};

} // namespace phigros::render
