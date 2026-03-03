#pragma once
// Platform detection and native window handle extraction.
// Used by bgfx to get the native display/window for GPU initialization.

#include "phigros/app/sdl_compat.hpp"

#if defined(PHIGROS_SDL3)
// SDL3 properties API for native handles
#elif defined(PHIGROS_SDL2)
#if !defined(PHIGROS_WASM)
#include <SDL2/SDL_syswm.h>
#endif
#endif

namespace phigros::app {

struct NativeHandles {
    void* window  = nullptr; // HWND / X11 Window / NSWindow / ANativeWindow
    void* display = nullptr; // X11 Display* / nullptr on other platforms
};

inline NativeHandles get_native_handles(SDL_Window* win) {
    NativeHandles nh;
    if (!win) return nh;

#if defined(PHIGROS_WASM)
    // Emscripten: no native handle needed, bgfx uses canvas directly
    (void)win;
#elif defined(PHIGROS_SDL3)
    auto props = SDL_GetWindowProperties(win);
    // Try X11 first, then Wayland, then others
    nh.window = SDL_GetPointerProperty(props,
        SDL_PROP_WINDOW_X11_WINDOW_POINTER, nullptr);
    nh.display = SDL_GetPointerProperty(props,
        SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    if (!nh.window) {
        // Wayland
        nh.window = SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        nh.display = SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    }
#elif defined(PHIGROS_SDL2)
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (SDL_GetWindowWMInfo(win, &wmi)) {
#if defined(__linux__)
        if (wmi.subsystem == SDL_SYSWM_X11) {
            nh.window = reinterpret_cast<void*>(wmi.info.x11.window);
            nh.display = wmi.info.x11.display;
        }
#elif defined(_WIN32)
        nh.window = wmi.info.win.window;
#elif defined(__APPLE__)
        nh.window = wmi.info.cocoa.window;
#elif defined(__ANDROID__)
        nh.window = wmi.info.android.window;
#endif
    }
#endif
    return nh;
}

} // namespace phigros::app

// --- Touch input for mobile ---
#if defined(PHIGROS_MOBILE) || defined(PHIGROS_TOUCH_INPUT)

namespace phigros::app {

struct TouchPoint {
    int64_t finger_id = -1;
    float x = 0, y = 0;       // logical pixels
    float pressure = 1.0f;
    bool active = false;
};

struct TouchState {
    static constexpr int MAX_FINGERS = 10;
    TouchPoint fingers[MAX_FINGERS];
    int active_count = 0;

    void process_event(const SDL_Event& e, int screen_w, int screen_h) {
#if defined(PHIGROS_SDL3)
        if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION) {
            auto& tf = e.tfinger;
            float px = tf.x * screen_w;
            float py = tf.y * screen_h;
            // Find existing or free slot
            int slot = find_slot(tf.fingerID);
            if (slot < 0) slot = find_free_slot();
            if (slot >= 0) {
                fingers[slot] = {tf.fingerID, px, py, tf.pressure, true};
                recount();
            }
        } else if (e.type == SDL_EVENT_FINGER_UP) {
            int slot = find_slot(e.tfinger.fingerID);
            if (slot >= 0) { fingers[slot].active = false; recount(); }
        }
#elif defined(PHIGROS_SDL2)
        if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION) {
            float px = e.tfinger.x * screen_w;
            float py = e.tfinger.y * screen_h;
            int slot = find_slot(e.tfinger.fingerId);
            if (slot < 0) slot = find_free_slot();
            if (slot >= 0) {
                fingers[slot] = {e.tfinger.fingerId, px, py, e.tfinger.pressure, true};
                recount();
            }
        } else if (e.type == SDL_FINGERUP) {
            int slot = find_slot(e.tfinger.fingerId);
            if (slot >= 0) { fingers[slot].active = false; recount(); }
        }
#endif
    }

private:
    int find_slot(int64_t fid) const {
        for (int i = 0; i < MAX_FINGERS; ++i)
            if (fingers[i].active && fingers[i].finger_id == fid) return i;
        return -1;
    }
    int find_free_slot() const {
        for (int i = 0; i < MAX_FINGERS; ++i)
            if (!fingers[i].active) return i;
        return -1;
    }
    void recount() {
        active_count = 0;
        for (int i = 0; i < MAX_FINGERS; ++i)
            if (fingers[i].active) ++active_count;
    }
};

} // namespace phigros::app

#endif // PHIGROS_MOBILE || PHIGROS_TOUCH_INPUT
