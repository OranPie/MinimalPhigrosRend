#pragma once
#include <SDL2/SDL.h>
#include "phigros/render/texture.hpp"
#include <cmath>
#include <algorithm>

namespace phigros::render {

// SpriteBatch wraps SDL_Renderer for convenient 2D drawing.
// SDL2_Renderer is already hardware-accelerated (OpenGL/Vulkan/Metal behind the scenes).
struct SpriteBatch {
    SDL_Renderer* ren = nullptr;

    void init(SDL_Renderer* renderer) { ren = renderer; }

    // Draw a textured quad with rotation, color tint, and alpha
    void draw_texture(const Texture& tex,
                      double x, double y, double w, double h,
                      double angle_rad = 0.0,
                      uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                      uint8_t a = 255,
                      const SDL_Rect* src_rect = nullptr,
                      SDL_RendererFlip flip = SDL_FLIP_NONE) const {
        if (!tex.tex) return;
        tex.set_color_mod(r, g, b);
        tex.set_alpha_mod(a);

        SDL_FRect dst;
        dst.x = static_cast<float>(x - w * 0.5);
        dst.y = static_cast<float>(y - h * 0.5);
        dst.w = static_cast<float>(w);
        dst.h = static_cast<float>(h);

        double angle_deg = angle_rad * (180.0 / M_PI);
        SDL_RenderCopyExF(ren, tex.tex, src_rect, &dst, angle_deg, nullptr, flip);
    }

    // Draw a textured sub-region (atlas sampling)
    void draw_texture_region(const Texture& tex,
                             int sx, int sy, int sw, int sh,
                             double x, double y, double dw, double dh,
                             double angle_rad = 0.0,
                             uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                             uint8_t a = 255) const {
        SDL_Rect src{sx, sy, sw, sh};
        draw_texture(tex, x, y, dw, dh, angle_rad, r, g, b, a, &src);
    }

    // Draw filled rectangle
    void draw_rect(double x, double y, double w, double h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) const {
        SDL_SetRenderDrawColor(ren, r, g, b, a);
        SDL_FRect rect;
        rect.x = static_cast<float>(x);
        rect.y = static_cast<float>(y);
        rect.w = static_cast<float>(w);
        rect.h = static_cast<float>(h);
        SDL_RenderFillRectF(ren, &rect);
    }

    // Draw a thick rotated line (as a rotated rectangle)
    void draw_line(double x0, double y0, double x1, double y1,
                   double width, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t a = 255) const {
        double dx = x1 - x0, dy = y1 - y0;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5) return;

        double angle_rad = std::atan2(dy, dx);
        double cx = (x0 + x1) * 0.5;
        double cy = (y0 + y1) * 0.5;

        // Use a 1x1 white texture stretched to the line dimensions
        // For simplicity, use draw primitives
        SDL_SetRenderDrawColor(ren, r, g, b, a);

        // Approximate thick line with multiple parallel lines
        double nx = -dy / len, ny = dx / len;
        int half = static_cast<int>(width * 0.5);
        for (int i = -half; i <= half; ++i) {
            double off = static_cast<double>(i);
            SDL_RenderDrawLineF(ren,
                static_cast<float>(x0 + nx * off),
                static_cast<float>(y0 + ny * off),
                static_cast<float>(x1 + nx * off),
                static_cast<float>(y1 + ny * off));
        }
    }

    // Draw a rotated filled rectangle (using a 1-pixel white texture)
    void draw_rotated_rect(const Texture& white_tex,
                           double cx, double cy, double w, double h,
                           double angle_rad,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) const {
        draw_texture(white_tex, cx, cy, w, h, angle_rad, r, g, b, a);
    }
};

} // namespace phigros::render
