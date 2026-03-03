#pragma once
// SDL3Renderer: IRenderer implementation using SDL3 (or SDL2) 2D renderer.
// This wraps the existing SpriteBatch + Window approach behind IRenderer.

#include "phigros/render/i_renderer.hpp"
#include "phigros/app/sdl_compat.hpp"
#include <unordered_map>
#include <vector>
#include <cstring>

namespace phigros::render {

struct SdlTextureEntry {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

struct SdlRenderer final : public IRenderer {
    SDL_Renderer* ren = nullptr;
    bool owns_renderer = false;
    std::unordered_map<TextureHandle, SdlTextureEntry> textures;
    uint64_t next_handle = 1;

    // Construct wrapping an existing SDL_Renderer (from Window)
    void set_renderer(SDL_Renderer* r) { ren = r; owns_renderer = false; }

    bool init(void* /*native_window*/, int /*w*/, int /*h*/) override {
        return ren != nullptr;
    }

    void shutdown() override {
        for (auto& [h, e] : textures) {
            if (e.tex) SDL_DestroyTexture(e.tex);
        }
        textures.clear();
        if (owns_renderer && ren) { SDL_DestroyRenderer(ren); ren = nullptr; }
    }

    void begin_frame(int /*w*/, int /*h*/) override {
        app::sdl::set_draw_color(ren, 10, 10, 14, 255);
        SDL_RenderClear(ren);
    }

    void end_frame() override {
        SDL_RenderPresent(ren);
    }

    TextureHandle create_texture(const uint8_t* rgba, int w, int h) override {
        SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STATIC, w, h);
        if (!t) return INVALID_TEXTURE;
        SDL_UpdateTexture(t, nullptr, rgba, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        TextureHandle handle = next_handle++;
        textures[handle] = {t, w, h};
        return handle;
    }

    void destroy_texture(TextureHandle h) override {
        auto it = textures.find(h);
        if (it != textures.end()) {
            if (it->second.tex) SDL_DestroyTexture(it->second.tex);
            textures.erase(it);
        }
    }

    void set_texture_color(TextureHandle h, uint8_t r, uint8_t g, uint8_t b) override {
        auto it = textures.find(h);
        if (it != textures.end() && it->second.tex)
            SDL_SetTextureColorMod(it->second.tex, r, g, b);
    }

    void set_texture_alpha(TextureHandle h, uint8_t a) override {
        auto it = textures.find(h);
        if (it != textures.end() && it->second.tex)
            SDL_SetTextureAlphaMod(it->second.tex, a);
    }

    void draw_quad(TextureHandle tex,
                   float cx, float cy, float w, float h,
                   float angle_rad,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        auto it = textures.find(tex);
        if (it == textures.end()) return;
        auto& e = it->second;
        SDL_SetTextureColorMod(e.tex, r, g, b);
        SDL_SetTextureAlphaMod(e.tex, a);
        SDL_FRect dst{cx - w * 0.5f, cy - h * 0.5f, w, h};
        float angle_deg = angle_rad * (180.0f / 3.14159265f);
        app::sdl::render_copy_ex(ren, e.tex, nullptr, &dst,
                                 static_cast<double>(angle_deg), PHIGROS_FLIP_NONE);
    }

    void draw_quad_region(TextureHandle tex,
                          int sx, int sy, int sw, int sh,
                          float cx, float cy, float dw, float dh,
                          float angle_rad,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        auto it = textures.find(tex);
        if (it == textures.end()) return;
        auto& e = it->second;
        SDL_SetTextureColorMod(e.tex, r, g, b);
        SDL_SetTextureAlphaMod(e.tex, a);
        SDL_Rect src{sx, sy, sw, sh};
        SDL_FRect dst{cx - dw * 0.5f, cy - dh * 0.5f, dw, dh};
        float angle_deg = angle_rad * (180.0f / 3.14159265f);
        app::sdl::render_copy_ex(ren, e.tex, &src, &dst,
                                 static_cast<double>(angle_deg), PHIGROS_FLIP_NONE);
    }

    void draw_rect(float x, float y, float w, float h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        app::sdl::set_draw_color(ren, r, g, b, a);
        SDL_FRect rect{x, y, w, h};
        app::sdl::render_fill_rect(ren, &rect);
    }

    void draw_line(float x0, float y0, float x1, float y1,
                   float width,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) return;
        app::sdl::set_draw_color(ren, r, g, b, a);
        float nx = -dy / len, ny = dx / len;
        int half = static_cast<int>(width * 0.5f);
        for (int i = -half; i <= half; ++i) {
            float off = static_cast<float>(i);
            app::sdl::render_draw_line(ren,
                x0 + nx * off, y0 + ny * off,
                x1 + nx * off, y1 + ny * off);
        }
    }

    bool read_pixels(uint8_t* rgba_out, int w, int h) override {
        return app::sdl::read_pixels_rgba(ren, rgba_out, w, h);
    }

    const char* backend_name() const override {
#if defined(PHIGROS_SDL3)
        return "SDL3";
#else
        return "SDL2";
#endif
    }
};

} // namespace phigros::render
