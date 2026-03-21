#pragma once
// SDL compatibility layer: enables building with either SDL2 or SDL3.
// All rendering code should include this instead of SDL headers directly.
#include <cstring>  // std::memcpy used in read_pixels_rgba
#include <algorithm>

#if defined(PHIGROS_SDL3)
#include <SDL3/SDL.h>

// SDL3 changed these names
#define PHIGROS_SDL_EVENT_QUIT       SDL_EVENT_QUIT
#define PHIGROS_SDL_EVENT_KEY_DOWN   SDL_EVENT_KEY_DOWN
#define PHIGROS_SDL_EVENT_KEY_UP     SDL_EVENT_KEY_UP
#define PHIGROS_SDL_EVENT_WINDOW_RESIZED SDL_EVENT_WINDOW_RESIZED
#define PHIGROS_SDL_MOUSE_DOWN   SDL_EVENT_MOUSE_BUTTON_DOWN
#define PHIGROS_SDL_MOUSE_UP     SDL_EVENT_MOUSE_BUTTON_UP
#define PHIGROS_SDL_MOUSE_MOVE   SDL_EVENT_MOUSE_MOTION
#define PHIGROS_SDL_FINGER_DOWN  SDL_EVENT_FINGER_DOWN
#define PHIGROS_SDL_FINGER_UP    SDL_EVENT_FINGER_UP
#define PHIGROS_SDL_FINGER_MOVE  SDL_EVENT_FINGER_MOTION
#define PHIGROS_FINGER_ID(e)     ((e).tfinger.fingerID)
#define PHIGROS_MOUSE_X(e)       static_cast<float>((e).button.x)
#define PHIGROS_MOUSE_Y(e)       static_cast<float>((e).button.y)
#define PHIGROS_MOTION_X(e)      static_cast<float>((e).motion.x)
#define PHIGROS_MOTION_Y(e)      static_cast<float>((e).motion.y)
#define PHIGROS_KEY_SCANCODE(e)  ((e).key.scancode)

namespace phigros::app::sdl {

struct ReadbackTiming {
    double api_ms = 0.0;      // SDL_RenderReadPixels
    double convert_ms = 0.0;  // pixel format conversion
    double copy_ms = 0.0;     // row copy / resample copy
    double total_ms = 0.0;    // full readback path
    int src_w = 0;
    int src_h = 0;
    bool converted = false;
    bool resampled = false;
};

inline bool sdl_init() {
    return SDL_Init(SDL_INIT_VIDEO);
}

inline SDL_Window* create_window(const char* title, int w, int h, bool hidden) {
    Uint32 flags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_HIGH_PIXEL_DENSITY;
    return SDL_CreateWindow(title, w, h, flags);
}

inline SDL_Renderer* create_renderer(SDL_Window* win, bool software, bool vsync = true) {
    if (software)
        return SDL_CreateRenderer(win, "software");
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (ren) SDL_SetRenderVSync(ren, vsync ? 1 : 0);
    return ren;
}

inline void set_render_logical_size(SDL_Renderer* ren, int w, int h) {
    SDL_SetRenderLogicalPresentation(ren, w, h,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

inline void render_copy_ex(SDL_Renderer* ren, SDL_Texture* tex,
                           const SDL_Rect* src, const SDL_FRect* dst,
                           double angle_deg, SDL_FlipMode flip) {
    if (src) {
        SDL_FRect fsrc{static_cast<float>(src->x), static_cast<float>(src->y),
                       static_cast<float>(src->w), static_cast<float>(src->h)};
        SDL_RenderTextureRotated(ren, tex, &fsrc, dst, angle_deg, nullptr, flip);
    } else {
        SDL_RenderTextureRotated(ren, tex, nullptr, dst, angle_deg, nullptr, flip);
    }
}

inline void render_copy(SDL_Renderer* ren, SDL_Texture* tex,
                        const SDL_Rect* src, const SDL_FRect* dst) {
    if (src) {
        SDL_FRect fsrc{static_cast<float>(src->x), static_cast<float>(src->y),
                       static_cast<float>(src->w), static_cast<float>(src->h)};
        SDL_RenderTexture(ren, tex, &fsrc, dst);
    } else {
        SDL_RenderTexture(ren, tex, nullptr, dst);
    }
}

inline void render_fill_rect(SDL_Renderer* ren, const SDL_FRect* rect) {
    SDL_RenderFillRect(ren, rect);
}

inline void render_draw_line(SDL_Renderer* ren, float x0, float y0,
                             float x1, float y1) {
    SDL_RenderLine(ren, x0, y0, x1, y1);
}

inline void set_draw_color(SDL_Renderer* ren, uint8_t r, uint8_t g,
                           uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(ren, r, g, b, a);
}

// SDL3: RenderReadPixels returns an SDL_Surface*
inline bool read_pixels_rgba(SDL_Renderer* ren, uint8_t* out, int w, int h,
                             ReadbackTiming* timing = nullptr) {
    const uint64_t t_begin = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    auto ticks_to_ms = [freq](uint64_t ticks) -> double {
        return static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq);
    };

    const uint64_t t_api0 = SDL_GetPerformanceCounter();
    SDL_Surface* surf = SDL_RenderReadPixels(ren, nullptr);
    const uint64_t t_api1 = SDL_GetPerformanceCounter();
    if (!surf) return false;
    SDL_Surface* rgba = surf;
    if (timing) {
        timing->api_ms = ticks_to_ms(t_api1 - t_api0);
        timing->src_w = surf->w;
        timing->src_h = surf->h;
    }
    // Convert to RGBA32 if needed so the resample/copy path can assume 4 bytes/pixel.
    if (surf->format != SDL_PIXELFORMAT_RGBA32) {
        const uint64_t t_conv0 = SDL_GetPerformanceCounter();
        rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        const uint64_t t_conv1 = SDL_GetPerformanceCounter();
        SDL_DestroySurface(surf);
        if (!rgba) return false;
        if (timing) {
            timing->convert_ms = ticks_to_ms(t_conv1 - t_conv0);
            timing->converted = true;
        }
    }

    SDL_LockSurface(rgba);
    const uint8_t* src = static_cast<const uint8_t*>(rgba->pixels);
    const int src_w = rgba->w;
    const int src_h = rgba->h;
    const int src_pitch = rgba->pitch;

    if (src_w == w && src_h == h) {
        const uint64_t t_copy0 = SDL_GetPerformanceCounter();
        for (int y = 0; y < h; ++y) {
            std::memcpy(out + static_cast<size_t>(y) * w * 4,
                        src + static_cast<size_t>(y) * src_pitch,
                        static_cast<size_t>(w) * 4);
        }
        const uint64_t t_copy1 = SDL_GetPerformanceCounter();
        SDL_UnlockSurface(rgba);
        SDL_DestroySurface(rgba);
        if (timing) {
            timing->copy_ms = ticks_to_ms(t_copy1 - t_copy0);
            timing->total_ms = ticks_to_ms(t_copy1 - t_begin);
        }
        return true;
    }

    // Resample to the logical render size expected by the recorder.
    const uint64_t t_copy0 = SDL_GetPerformanceCounter();
    for (int y = 0; y < h; ++y) {
        const int sy = std::clamp((y * src_h) / std::max(1, h), 0, std::max(0, src_h - 1));
        const uint8_t* src_row = src + static_cast<size_t>(sy) * src_pitch;
        uint8_t* dst_row = out + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const int sx = std::clamp((x * src_w) / std::max(1, w), 0, std::max(0, src_w - 1));
            const uint8_t* src_px = src_row + static_cast<size_t>(sx) * 4;
            uint8_t* dst_px = dst_row + static_cast<size_t>(x) * 4;
            dst_px[0] = src_px[0];
            dst_px[1] = src_px[1];
            dst_px[2] = src_px[2];
            dst_px[3] = src_px[3];
        }
    }
    const uint64_t t_copy1 = SDL_GetPerformanceCounter();

    SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
    if (timing) {
        timing->copy_ms = ticks_to_ms(t_copy1 - t_copy0);
        timing->total_ms = ticks_to_ms(t_copy1 - t_begin);
        timing->resampled = true;
    }
    return true;
}

inline bool save_screenshot_bmp(SDL_Renderer* ren, const char* path, int w, int h) {
    SDL_Surface* surf = SDL_RenderReadPixels(ren, nullptr);
    if (!surf) return false;
    bool ok = SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    return ok;
}

inline bool handle_event_quit(const SDL_Event& e) {
    return e.type == SDL_EVENT_QUIT;
}

inline bool handle_event_key_escape(const SDL_Event& e) {
    return e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE;
}

inline bool handle_event_window_resized(const SDL_Event& e, int& w, int& h) {
    if (e.type == SDL_EVENT_WINDOW_RESIZED) {
        w = e.window.data1;
        h = e.window.data2;
        return true;
    }
    return false;
}

} // namespace phigros::app::sdl

#elif defined(PHIGROS_SDL2)
#if defined(__EMSCRIPTEN__)
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#define PHIGROS_SDL_EVENT_QUIT       SDL_QUIT
#define PHIGROS_SDL_EVENT_KEY_DOWN   SDL_KEYDOWN
#define PHIGROS_SDL_EVENT_KEY_UP     SDL_KEYUP
#define PHIGROS_SDL_MOUSE_DOWN   SDL_MOUSEBUTTONDOWN
#define PHIGROS_SDL_MOUSE_UP     SDL_MOUSEBUTTONUP
#define PHIGROS_SDL_MOUSE_MOVE   SDL_MOUSEMOTION
#define PHIGROS_SDL_FINGER_DOWN  SDL_FINGERDOWN
#define PHIGROS_SDL_FINGER_UP    SDL_FINGERUP
#define PHIGROS_SDL_FINGER_MOVE  SDL_FINGERMOTION
#define PHIGROS_FINGER_ID(e)     ((e).tfinger.fingerId)
#define PHIGROS_MOUSE_X(e)       static_cast<float>((e).button.x)
#define PHIGROS_MOUSE_Y(e)       static_cast<float>((e).button.y)
#define PHIGROS_MOTION_X(e)      static_cast<float>((e).motion.x)
#define PHIGROS_MOTION_Y(e)      static_cast<float>((e).motion.y)
#define PHIGROS_KEY_SCANCODE(e)  ((e).key.keysym.scancode)

namespace phigros::app::sdl {

struct ReadbackTiming {
    double api_ms = 0.0;
    double convert_ms = 0.0;
    double copy_ms = 0.0;
    double total_ms = 0.0;
    int src_w = 0;
    int src_h = 0;
    bool converted = false;
    bool resampled = false;
};

inline bool sdl_init() {
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0;
}

inline SDL_Window* create_window(const char* title, int w, int h, bool hidden) {
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (hidden) flags = SDL_WINDOW_HIDDEN;
    return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w, h, flags);
}

inline SDL_Renderer* create_renderer(SDL_Window* win, bool software, bool vsync = true) {
    if (software)
        return SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    return SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
}

inline void set_render_logical_size(SDL_Renderer* ren, int w, int h) {
    SDL_RenderSetLogicalSize(ren, w, h);
}

inline void render_copy_ex(SDL_Renderer* ren, SDL_Texture* tex,
                           const SDL_Rect* src, const SDL_FRect* dst,
                           double angle_deg, SDL_RendererFlip flip) {
    SDL_RenderCopyExF(ren, tex, src, dst, angle_deg, nullptr, flip);
}

inline void render_copy(SDL_Renderer* ren, SDL_Texture* tex,
                        const SDL_Rect* src, const SDL_FRect* dst) {
    SDL_RenderCopyF(ren, tex, src, dst);
}

inline void render_fill_rect(SDL_Renderer* ren, const SDL_FRect* rect) {
    SDL_RenderFillRectF(ren, rect);
}

inline void render_draw_line(SDL_Renderer* ren, float x0, float y0,
                             float x1, float y1) {
    SDL_RenderDrawLineF(ren, x0, y0, x1, y1);
}

inline void set_draw_color(SDL_Renderer* ren, uint8_t r, uint8_t g,
                           uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(ren, r, g, b, a);
}

inline bool read_pixels_rgba(SDL_Renderer* ren, uint8_t* out, int w, int h,
                             ReadbackTiming* timing = nullptr) {
    const uint64_t t0 = SDL_GetPerformanceCounter();
    const int ok = SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                        out, w * 4);
    const uint64_t t1 = SDL_GetPerformanceCounter();
    if (timing) {
        const double ms = static_cast<double>(t1 - t0) * 1000.0 /
                          static_cast<double>(SDL_GetPerformanceFrequency());
        timing->api_ms = ms;
        timing->total_ms = ms;
        timing->src_w = w;
        timing->src_h = h;
    }
    return ok == 0;
}

inline bool save_screenshot_bmp(SDL_Renderer* ren, const char* path, int w, int h) {
    SDL_Surface* sshot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                        SDL_PIXELFORMAT_RGBA32);
    if (!sshot) return false;
    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                         sshot->pixels, sshot->pitch);
    int ret = SDL_SaveBMP(sshot, path);
    SDL_FreeSurface(sshot);
    return ret == 0;
}

inline bool handle_event_quit(const SDL_Event& e) {
    return e.type == SDL_QUIT;
}

inline bool handle_event_key_escape(const SDL_Event& e) {
    return e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE;
}

inline bool handle_event_window_resized(const SDL_Event& e, int& w, int& h) {
    if (e.type == SDL_WINDOWEVENT &&
        e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        w = e.window.data1;
        h = e.window.data2;
        return true;
    }
    return false;
}

} // namespace phigros::app::sdl

#else
#error "Define PHIGROS_SDL2 or PHIGROS_SDL3"
#endif

// Common type alias for flip mode
#if defined(PHIGROS_SDL3)
using PhigrosFlipMode = SDL_FlipMode;
#define PHIGROS_FLIP_NONE SDL_FLIP_NONE
#else
using PhigrosFlipMode = SDL_RendererFlip;
#define PHIGROS_FLIP_NONE SDL_FLIP_NONE
#endif
