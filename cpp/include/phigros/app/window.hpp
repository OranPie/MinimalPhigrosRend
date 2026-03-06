#pragma once
#include "phigros/app/sdl_compat.hpp"
#include <string>
#include <stdexcept>
#include <cstring>
#include <vector>
#include "stb_image_write.h"

namespace phigros::app {

struct Window {
    SDL_Window*   win  = nullptr;
    SDL_Renderer* ren  = nullptr;
    int w = 1280, h = 720;
    bool quit_requested = false;
    bool resized = false;
    std::vector<SDL_Event> last_events;  // all events from last poll_events()

    void init(int width, int height, const std::string& title = "Phigros Renderer",
              bool headless = false, bool vsync = true) {
        w = width; h = height;
        if (!sdl::sdl_init())
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

        win = sdl::create_window(title.c_str(), w, h, headless);
        if (!win) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

        ren = sdl::create_renderer(win, false, vsync);
        if (!ren) ren = sdl::create_renderer(win, true, false); // software fallback, no vsync concern
        if (!ren) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        sdl::set_render_logical_size(ren, w, h);
    }

    void poll_events() {
        SDL_Event e;
        resized = false;
        last_events.clear();
        while (SDL_PollEvent(&e)) {
            last_events.push_back(e);
            if (sdl::handle_event_quit(e)) quit_requested = true;
            if (sdl::handle_event_key_escape(e)) quit_requested = true;
            sdl::handle_event_window_resized(e, w, h);
        }
    }

    void begin_frame() {
        sdl::set_draw_color(ren, 10, 10, 14, 255);
        SDL_RenderClear(ren);
    }

    void end_frame() {
        SDL_RenderPresent(ren);
    }

    bool save_screenshot(const std::string& path) const {
        return sdl::save_screenshot_bmp(ren, path.c_str(), w, h);
    }

    // Save current frame as PNG (RGBA readback → stb_image_write).
    bool save_screenshot_png(const std::string& path) const {
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (!read_pixels_rgba(pixels.data())) return false;
        return stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4) != 0;
    }

    bool read_pixels_rgba(uint8_t* out) const {
        return sdl::read_pixels_rgba(ren, out, w, h);
    }

    void destroy() {
        if (ren) { SDL_DestroyRenderer(ren); ren = nullptr; }
        if (win) { SDL_DestroyWindow(win); win = nullptr; }
        SDL_Quit();
    }

    static double get_time_sec() {
        return static_cast<double>(SDL_GetPerformanceCounter()) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    }
};

} // namespace phigros::app
