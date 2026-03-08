#pragma once
#include "phigros/app/sdl_compat.hpp"
#include <cstdint>

namespace phigros::render {

// Offscreen render target backed by SDL_TEXTUREACCESS_TARGET.
// Used by TrailRenderer and MotionBlurRenderer for GPU-side compositing.
struct RenderTarget {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;

    bool valid() const { return tex != nullptr; }

    // Redirect subsequent SDL draws into this texture.
    void bind(SDL_Renderer* ren) const {
        SDL_SetRenderTarget(ren, tex);
    }

    // Restore drawing to the window backbuffer.
    static void unbind(SDL_Renderer* ren) {
        SDL_SetRenderTarget(ren, nullptr);
    }

    // Bind + clear to a specific RGBA colour (default: transparent black).
    void clear(SDL_Renderer* ren,
               uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 0) const {
        bind(ren);
        SDL_SetRenderDrawColor(ren, r, g, b, a);
        SDL_RenderClear(ren);
    }

    // Blit this texture onto the *current* render target at full-screen size.
    void blit_to(SDL_Renderer* ren, uint8_t alpha,
                 SDL_BlendMode blend = SDL_BLENDMODE_BLEND) const {
        if (!tex) return;
        // Textures rendered via SDL render targets are effectively premultiplied-alpha
        // buffers. Re-blending them with regular SRC_ALPHA causes an extra alpha
        // multiply (visible as dark/near-black output in motion-blur accumulation).
        // Use ONE, ONE_MINUS_SRC_ALPHA for "normal" blits of render targets.
        if (blend == SDL_BLENDMODE_BLEND) {
            blend = SDL_ComposeCustomBlendMode(
                SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        }
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureBlendMode(tex, blend);
        SDL_FRect dst{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
        app::sdl::render_copy(ren, tex, nullptr, &dst);
    }

    void destroy() {
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        w = h = 0;
    }

    static RenderTarget create(SDL_Renderer* ren, int width, int height) {
        SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET, width, height);
        if (!t) return {};
        return {t, width, height};
    }
};

} // namespace phigros::render
