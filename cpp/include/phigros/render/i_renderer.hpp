#pragma once
// Backend-agnostic rendering interface.
// Implementations: SDL3Renderer (software/HW 2D), BgfxRenderer (GPU shaders).

#include <cstdint>
#include <string>
#include <cmath>

namespace phigros::render {

using TextureHandle = uint64_t;
constexpr TextureHandle INVALID_TEXTURE = 0;

struct IRenderer {
    virtual ~IRenderer() = default;

    // Lifecycle
    virtual bool init(void* native_window, int w, int h) = 0;
    virtual void shutdown() = 0;
    virtual void begin_frame(int w, int h) = 0;
    virtual void end_frame() = 0;

    // Texture management
    virtual TextureHandle create_texture(const uint8_t* rgba, int w, int h) = 0;
    virtual void destroy_texture(TextureHandle h) = 0;
    virtual void set_texture_color(TextureHandle h, uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void set_texture_alpha(TextureHandle h, uint8_t a) = 0;

    // Drawing (coordinates in logical pixels, origin top-left)
    virtual void draw_quad(TextureHandle tex,
                           float cx, float cy, float w, float h,
                           float angle_rad = 0.f,
                           uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                           uint8_t a = 255) = 0;

    virtual void draw_quad_region(TextureHandle tex,
                                  int sx, int sy, int sw, int sh,
                                  float cx, float cy, float dw, float dh,
                                  float angle_rad = 0.f,
                                  uint8_t r = 255, uint8_t g = 255,
                                  uint8_t b = 255, uint8_t a = 255) = 0;

    virtual void draw_rect(float x, float y, float w, float h,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) = 0;

    virtual void draw_line(float x0, float y0, float x1, float y1,
                           float width,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) = 0;

    // Pixel readback (for recording/screenshots)
    virtual bool read_pixels(uint8_t* rgba_out, int w, int h) = 0;

    // Backend info
    virtual const char* backend_name() const = 0;
};

} // namespace phigros::render
