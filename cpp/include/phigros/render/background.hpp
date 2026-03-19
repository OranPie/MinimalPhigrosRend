#pragma once
#include "phigros/render/texture.hpp"
#include "phigros/render/sprite_batch.hpp"
#include <string>
#include <cstdint>
#include <algorithm>

// Forward-declare stb_image
extern "C" {
    unsigned char* stbi_load(const char*, int*, int*, int*, int);
    unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
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

        bg_tex = Texture::from_rgba(ren, pixels, iw, ih);
        stbi_image_free(pixels);
        (void)blur_factor;
        has_bg = bg_tex.valid();
    }

    void load_from_memory(SDL_Renderer* ren, const uint8_t* data, int len, int sw, int sh) {
        screen_w = sw; screen_h = sh;
        int iw, ih, ch;
        uint8_t* pixels = stbi_load_from_memory(data, len, &iw, &ih, &ch, 4);
        if (!pixels) return;
        bg_tex = Texture::from_rgba(ren, pixels, iw, ih);
        stbi_image_free(pixels);
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
