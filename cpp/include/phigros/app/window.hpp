#pragma once
#include "phigros/app/sdl_compat.hpp"
#include <string>
#include <stdexcept>
#include <cstring>

namespace phigros::app {

struct Window {
    SDL_Window*   win  = nullptr;
    SDL_Renderer* ren  = nullptr;
    int w = 1280, h = 720;
    bool quit_requested = false;
    bool resized = false;

    void init(int width, int height, const std::string& title = "Phigros Renderer",
              bool headless = false) {
        w = width; h = height;
        if (!sdl::sdl_init())
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

        win = sdl::create_window(title.c_str(), w, h, headless);
        if (!win) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

        ren = sdl::create_renderer(win, false);
        if (!ren) ren = sdl::create_renderer(win, true); // fallback
        if (!ren) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        sdl::set_render_logical_size(ren, w, h);
    }

    void poll_events() {
        SDL_Event e;
        resized = false;
        while (SDL_PollEvent(&e)) {
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
