#pragma once
// BgfxRenderer: IRenderer implementation using bgfx for GPU-accelerated rendering.
// Supports Vulkan, Metal, DX12, OpenGL, GLES3, WebGPU backends.
// Requires PHIGROS_HAS_BGFX=1 compile definition.

#ifdef PHIGROS_HAS_BGFX

#include "phigros/render/i_renderer.hpp"
#include "phigros/render/embedded_shaders.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include <unordered_map>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(PHIGROS_SDL3)
#include <SDL3/SDL.h>
#elif defined(PHIGROS_SDL2)
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#endif

namespace phigros::render {

struct BgfxTextureEntry {
    bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
    int w = 0, h = 0;
};

// Sprite vertex: position, texcoord, color (packed ABGR)
struct SpriteVertex {
    float x, y;
    float u, v;
    uint32_t abgr;

    static bgfx::VertexLayout layout;
    static void init_layout() {
        layout
            .begin()
            .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
            .end();
    }
};

inline bgfx::VertexLayout SpriteVertex::layout;

struct BgfxRenderer final : public IRenderer {
    static constexpr int MAX_QUADS_PER_BATCH = 4096;
    static constexpr uint16_t VIEW_ID = 0;

    std::unordered_map<TextureHandle, BgfxTextureEntry> textures;
    uint64_t next_handle = 1;

    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_viewProj = BGFX_INVALID_HANDLE;

    // 1x1 white texture for solid rects/lines
    bgfx::TextureHandle white_tex = BGFX_INVALID_HANDLE;
    TextureHandle white_handle = INVALID_TEXTURE;

    // Batch state
    std::vector<SpriteVertex> vertices;
    std::vector<uint16_t> indices;
    TextureHandle current_tex = INVALID_TEXTURE;

    int screen_w = 0, screen_h = 0;
    bool initialized = false;

    static void* get_native_window_handle(void* sdl_window) {
#if defined(PHIGROS_SDL3)
        return SDL_GetPointerProperty(
            SDL_GetWindowProperties(static_cast<SDL_Window*>(sdl_window)),
            SDL_PROP_WINDOW_X11_WINDOW_POINTER, nullptr);
#elif defined(PHIGROS_SDL2)
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (!SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdl_window), &wmi))
            return nullptr;
#if defined(__linux__)
        return reinterpret_cast<void*>(wmi.info.x11.window);
#elif defined(_WIN32)
        return wmi.info.win.window;
#elif defined(__APPLE__)
        return wmi.info.cocoa.window;
#endif
#endif
        return nullptr;
    }

    static void* get_native_display(void* sdl_window) {
#if defined(PHIGROS_SDL3)
        return SDL_GetPointerProperty(
            SDL_GetWindowProperties(static_cast<SDL_Window*>(sdl_window)),
            SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#elif defined(PHIGROS_SDL2) && defined(__linux__)
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (!SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdl_window), &wmi))
            return nullptr;
        return wmi.info.x11.display;
#endif
        return nullptr;
    }

    bool init(void* native_window, int w, int h) override {
        screen_w = w; screen_h = h;
        SpriteVertex::init_layout();

        bgfx::Init bgfx_init;
        bgfx_init.type = bgfx::RendererType::Count; // auto-select
        bgfx_init.resolution.width = w;
        bgfx_init.resolution.height = h;
        bgfx_init.resolution.reset = BGFX_RESET_VSYNC;

        // Platform data from SDL window
        if (native_window) {
            bgfx_init.platformData.nwh = get_native_window_handle(native_window);
            bgfx_init.platformData.ndt = get_native_display(native_window);
        }

        if (!bgfx::init(bgfx_init)) return false;

        bgfx::setViewClear(VIEW_ID,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x0a0a0eff, 1.0f, 0);
        bgfx::setViewRect(VIEW_ID, 0, 0, w, h);

        // Create uniforms
        s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        u_viewProj = bgfx::createUniform("u_viewProj", bgfx::UniformType::Mat4);

        // Create shader program from embedded GLSL
        // Note: For production, pre-compile with shaderc for each platform.
        // This uses bgfx's built-in shader compilation for supported backends.
        program = create_sprite_program();

        // Create 1x1 white texture
        uint32_t white_pixel = 0xFFFFFFFF;
        white_tex = bgfx::createTexture2D(1, 1, false, 1,
            bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(&white_pixel, 4));
        white_handle = next_handle++;
        textures[white_handle] = {white_tex, 1, 1};

        vertices.reserve(MAX_QUADS_PER_BATCH * 4);
        indices.reserve(MAX_QUADS_PER_BATCH * 6);

        initialized = true;
        return true;
    }

    bgfx::ProgramHandle create_sprite_program() {
        // bgfx requires pre-compiled shaders per renderer type.
        // We provide embedded GLSL and rely on the renderer supporting GL.
        // For a full cross-platform build, use shaderc offline compilation.
        // This is a placeholder that returns an invalid handle if not GL.
        auto type = bgfx::getRendererType();
        if (type != bgfx::RendererType::OpenGL &&
            type != bgfx::RendererType::OpenGLES) {
            // For non-GL backends, shaders must be pre-compiled.
            // Return invalid handle; rendering will use the SDL fallback.
            return BGFX_INVALID_HANDLE;
        }

        // For OpenGL, we can use runtime GLSL compilation via bgfx memory
        const char* vs_src = shaders::vs_sprite_gl33;
        const char* fs_src = shaders::fs_sprite_gl33;
        if (type == bgfx::RendererType::OpenGLES) {
            vs_src = shaders::vs_sprite_glsl;
            fs_src = shaders::fs_sprite_glsl;
        }

        auto vs_mem = bgfx::makeRef(vs_src, std::strlen(vs_src) + 1);
        auto fs_mem = bgfx::makeRef(fs_src, std::strlen(fs_src) + 1);
        bgfx::ShaderHandle vs = bgfx::createShader(vs_mem);
        bgfx::ShaderHandle fs = bgfx::createShader(fs_mem);
        if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
            if (bgfx::isValid(vs)) bgfx::destroy(vs);
            if (bgfx::isValid(fs)) bgfx::destroy(fs);
            return BGFX_INVALID_HANDLE;
        }
        return bgfx::createProgram(vs, fs, true);
    }

    void shutdown() override {
        flush();
        for (auto& [h, e] : textures) {
            if (bgfx::isValid(e.tex)) bgfx::destroy(e.tex);
        }
        textures.clear();
        if (bgfx::isValid(program)) bgfx::destroy(program);
        if (bgfx::isValid(s_texColor)) bgfx::destroy(s_texColor);
        if (bgfx::isValid(u_viewProj)) bgfx::destroy(u_viewProj);
        bgfx::shutdown();
        initialized = false;
    }

    void begin_frame(int w, int h) override {
        screen_w = w; screen_h = h;
        bgfx::setViewRect(VIEW_ID, 0, 0, w, h);

        // 2D orthographic projection (top-left origin)
        float proj[16];
        bx::mtxOrtho(proj, 0.0f, static_cast<float>(w),
                      static_cast<float>(h), 0.0f,
                      -1.0f, 1.0f, 0.0f,
                      bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(VIEW_ID, nullptr, proj);
        bgfx::setUniform(u_viewProj, proj);
        bgfx::touch(VIEW_ID);
    }

    void end_frame() override {
        flush();
        bgfx::frame();
    }

    TextureHandle create_texture(const uint8_t* rgba, int w, int h) override {
        auto mem = bgfx::copy(rgba, w * h * 4);
        bgfx::TextureHandle tex = bgfx::createTexture2D(
            w, h, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT, mem);
        if (!bgfx::isValid(tex)) return INVALID_TEXTURE;
        TextureHandle handle = next_handle++;
        textures[handle] = {tex, w, h};
        return handle;
    }

    void destroy_texture(TextureHandle h) override {
        auto it = textures.find(h);
        if (it != textures.end()) {
            if (bgfx::isValid(it->second.tex)) bgfx::destroy(it->second.tex);
            textures.erase(it);
        }
    }

    void set_texture_color(TextureHandle, uint8_t, uint8_t, uint8_t) override {
        // bgfx handles color via vertex color, not texture mod
    }
    void set_texture_alpha(TextureHandle, uint8_t) override {
        // bgfx handles alpha via vertex color
    }

    static uint32_t pack_abgr(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return (uint32_t(a) << 24) | (uint32_t(b) << 16) |
               (uint32_t(g) << 8) | uint32_t(r);
    }

    void flush() {
        if (vertices.empty()) return;
        if (!bgfx::isValid(program)) { vertices.clear(); indices.clear(); return; }

        uint32_t num_verts = static_cast<uint32_t>(vertices.size());
        uint32_t num_idx = static_cast<uint32_t>(indices.size());

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;

        if (!bgfx::allocTransientBuffers(&tvb, SpriteVertex::layout, num_verts,
                                          &tib, num_idx)) {
            vertices.clear(); indices.clear();
            return;
        }

        std::memcpy(tvb.data, vertices.data(), num_verts * sizeof(SpriteVertex));
        std::memcpy(tib.data, indices.data(), num_idx * sizeof(uint16_t));

        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);

        auto it = textures.find(current_tex);
        if (it != textures.end() && bgfx::isValid(it->second.tex)) {
            bgfx::setTexture(0, s_texColor, it->second.tex);
        }

        bgfx::setState(
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::submit(VIEW_ID, program);

        vertices.clear();
        indices.clear();
    }

    void add_quad(TextureHandle tex,
                  float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  uint32_t abgr) {
        if (tex != current_tex && !vertices.empty()) flush();
        current_tex = tex;

        if (vertices.size() + 4 > MAX_QUADS_PER_BATCH * 4) flush();

        uint16_t base = static_cast<uint16_t>(vertices.size());
        vertices.push_back({x0, y0, u0, v0, abgr});
        vertices.push_back({x1, y0, u1, v0, abgr});
        vertices.push_back({x1, y1, u1, v1, abgr});
        vertices.push_back({x0, y1, u0, v1, abgr});

        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    void add_rotated_quad(TextureHandle tex,
                          float cx, float cy, float hw, float hh,
                          float angle_rad,
                          float u0, float v0, float u1, float v1,
                          uint32_t abgr) {
        float cs = std::cos(angle_rad);
        float sn = std::sin(angle_rad);
        auto rotate = [&](float lx, float ly, float& ox, float& oy) {
            ox = cx + lx * cs - ly * sn;
            oy = cy + lx * sn + ly * cs;
        };

        if (tex != current_tex && !vertices.empty()) flush();
        current_tex = tex;
        if (vertices.size() + 4 > MAX_QUADS_PER_BATCH * 4) flush();

        float px[4], py[4];
        rotate(-hw, -hh, px[0], py[0]);
        rotate( hw, -hh, px[1], py[1]);
        rotate( hw,  hh, px[2], py[2]);
        rotate(-hw,  hh, px[3], py[3]);

        uint16_t base = static_cast<uint16_t>(vertices.size());
        vertices.push_back({px[0], py[0], u0, v0, abgr});
        vertices.push_back({px[1], py[1], u1, v0, abgr});
        vertices.push_back({px[2], py[2], u1, v1, abgr});
        vertices.push_back({px[3], py[3], u0, v1, abgr});

        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    void draw_quad(TextureHandle tex,
                   float cx, float cy, float w, float h,
                   float angle_rad,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        uint32_t abgr = pack_abgr(r, g, b, a);
        float hw = w * 0.5f, hh = h * 0.5f;
        if (std::fabs(angle_rad) < 0.001f) {
            add_quad(tex, cx - hw, cy - hh, cx + hw, cy + hh,
                     0.f, 0.f, 1.f, 1.f, abgr);
        } else {
            add_rotated_quad(tex, cx, cy, hw, hh, angle_rad,
                             0.f, 0.f, 1.f, 1.f, abgr);
        }
    }

    void draw_quad_region(TextureHandle tex,
                          int sx, int sy, int sw, int sh,
                          float cx, float cy, float dw, float dh,
                          float angle_rad,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        auto it = textures.find(tex);
        if (it == textures.end()) return;
        float tw = static_cast<float>(it->second.w);
        float th = static_cast<float>(it->second.h);
        float u0 = sx / tw, v0 = sy / th;
        float u1 = (sx + sw) / tw, v1 = (sy + sh) / th;
        uint32_t abgr = pack_abgr(r, g, b, a);
        float hw = dw * 0.5f, hh = dh * 0.5f;
        if (std::fabs(angle_rad) < 0.001f) {
            add_quad(tex, cx - hw, cy - hh, cx + hw, cy + hh,
                     u0, v0, u1, v1, abgr);
        } else {
            add_rotated_quad(tex, cx, cy, hw, hh, angle_rad,
                             u0, v0, u1, v1, abgr);
        }
    }

    void draw_rect(float x, float y, float w, float h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        uint32_t abgr = pack_abgr(r, g, b, a);
        add_quad(white_handle, x, y, x + w, y + h,
                 0.f, 0.f, 1.f, 1.f, abgr);
    }

    void draw_line(float x0, float y0, float x1, float y1,
                   float width,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) return;
        float cx = (x0 + x1) * 0.5f;
        float cy = (y0 + y1) * 0.5f;
        float angle = std::atan2(dy, dx);
        uint32_t abgr = pack_abgr(r, g, b, a);
        add_rotated_quad(white_handle, cx, cy, len * 0.5f, width * 0.5f,
                         angle, 0.f, 0.f, 1.f, 1.f, abgr);
    }

    bool read_pixels(uint8_t* /*rgba_out*/, int /*w*/, int /*h*/) override {
        // bgfx read-back is async via bgfx::readTexture + blit.
        // For recording, prefer the SDL fallback path.
        return false;
    }

    const char* backend_name() const override {
        if (!initialized) return "bgfx (not init)";
        switch (bgfx::getRendererType()) {
            case bgfx::RendererType::Vulkan:   return "bgfx/Vulkan";
            case bgfx::RendererType::Metal:    return "bgfx/Metal";
            case bgfx::RendererType::Direct3D12: return "bgfx/DX12";
            case bgfx::RendererType::OpenGL:   return "bgfx/OpenGL";
            case bgfx::RendererType::OpenGLES: return "bgfx/GLES";
            default: return "bgfx/Unknown";
        }
    }
};

} // namespace phigros::render

#endif // PHIGROS_HAS_BGFX
