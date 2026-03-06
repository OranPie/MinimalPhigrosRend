#pragma once
// SDL compatibility layer: enables building with either SDL2 or SDL3.
// All rendering code should include this instead of SDL headers directly.
#include <cstring>  // std::memcpy used in read_pixels_rgba

#if defined(PHIGROS_SDL3)
#include <SDL3/SDL.h>

// SDL3 changed these names
#define PHIGROS_SDL_EVENT_QUIT       SDL_EVENT_QUIT
#define PHIGROS_SDL_EVENT_KEY_DOWN   SDL_EVENT_KEY_DOWN
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

inline bool sdl_init() {
    return SDL_Init(SDL_INIT_VIDEO);
}

inline SDL_Window* create_window(const char* title, int w, int h, bool hidden) {
    Uint32 flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (hidden) flags |= SDL_WINDOW_HIDDEN;
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
inline bool read_pixels_rgba(SDL_Renderer* ren, uint8_t* out, int w, int h) {
    SDL_Surface* surf = SDL_RenderReadPixels(ren, nullptr);
    if (!surf) return false;
    // Fast path: surface is already RGBA32 (common for hardware renderers)
    if (surf->format == SDL_PIXELFORMAT_RGBA32) {
        SDL_LockSurface(surf);
        std::memcpy(out, surf->pixels, static_cast<size_t>(w) * h * 4);
        SDL_UnlockSurface(surf);
        SDL_DestroySurface(surf);
        return true;
    }
    // Slow path: need format conversion
    SDL_Surface* rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!rgba) return false;
    SDL_LockSurface(rgba);
    std::memcpy(out, rgba->pixels, static_cast<size_t>(w) * h * 4);
    SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
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

inline bool read_pixels_rgba(SDL_Renderer* ren, uint8_t* out, int w, int h) {
    return SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                out, w * 4) == 0;
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
