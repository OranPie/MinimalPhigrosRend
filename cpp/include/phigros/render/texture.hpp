#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <cstdint>

// Forward-declare stb_image functions (defined in vendor_impl.cpp)
extern "C" {
    unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    unsigned char* stbi_load(const char*, int*, int*, int*, int);
    void stbi_image_free(void*);
}

namespace phigros::render {

struct Texture {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;

    bool valid() const { return tex != nullptr; }

    void set_color_mod(uint8_t r, uint8_t g, uint8_t b) const {
        if (tex) SDL_SetTextureColorMod(tex, r, g, b);
    }
    void set_alpha_mod(uint8_t a) const {
        if (tex) SDL_SetTextureAlphaMod(tex, a);
    }
    void set_blend_mode(SDL_BlendMode mode) const {
        if (tex) SDL_SetTextureBlendMode(tex, mode);
    }

    void destroy() {
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        w = h = 0;
    }

    // Create from raw RGBA pixels
    static Texture from_rgba(SDL_Renderer* ren, const uint8_t* pixels,
                             int width, int height) {
        SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STATIC, width, height);
        if (!t) return {};
        SDL_UpdateTexture(t, nullptr, pixels, width * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        return {t, width, height};
    }

    // Load from image file on disk
    static Texture from_file(SDL_Renderer* ren, const std::string& path) {
        int w, h, ch;
        uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!pixels) return {};
        auto tex = from_rgba(ren, pixels, w, h);
        stbi_image_free(pixels);
        return tex;
    }

    // Load from in-memory data (PNG/JPG bytes)
    static Texture from_memory(SDL_Renderer* ren, const uint8_t* data, int len) {
        int w, h, ch;
        uint8_t* pixels = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
        if (!pixels) return {};
        auto tex = from_rgba(ren, pixels, w, h);
        stbi_image_free(pixels);
        return tex;
    }

    // Create a solid colored rectangle texture
    static Texture solid_rect(SDL_Renderer* ren, int width, int height,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        std::vector<uint8_t> pixels(width * height * 4);
        for (int i = 0; i < width * height; ++i) {
            pixels[i * 4 + 0] = r;
            pixels[i * 4 + 1] = g;
            pixels[i * 4 + 2] = b;
            pixels[i * 4 + 3] = a;
        }
        return from_rgba(ren, pixels.data(), width, height);
    }
};

} // namespace phigros::render
