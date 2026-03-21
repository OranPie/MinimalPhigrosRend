#pragma once
#include "phigros/app/sdl_compat.hpp"
#include <string>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cctype>
#include "stb_image_write.h"

namespace phigros::app {

struct Window {
    SDL_Window*   win  = nullptr;
    SDL_Renderer* ren  = nullptr;
    int w = 1280, h = 720;
    bool quit_requested = false;
    bool resized = false;
    std::vector<SDL_Event> last_events;  // all events from last poll_events()

    static std::string normalize_backend(std::string backend) {
        std::transform(backend.begin(), backend.end(), backend.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (backend == "software") return "sdl_sw";
        if (backend == "hardware") return "sdl_hw";
        if (backend == "bgfx") return "sdl_hw"; // Current runtime uses SDL renderer path.
        if (backend.empty()) return "sdl";
        return backend;
    }

    void init(int width, int height, const std::string& title = "Phigros Renderer",
              bool headless = false, bool vsync = true,
              const std::string& backend = "sdl") {
        w = width; h = height;
        if (!sdl::sdl_init())
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

        win = sdl::create_window(title.c_str(), w, h, headless);
        if (!win) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

        const std::string b = normalize_backend(backend);
        if (b != "sdl" && b != "sdl_hw" && b != "sdl_sw")
            throw std::runtime_error("Unsupported backend '" + backend +
                                     "'. Supported: sdl, sdl_hw, sdl_sw.");

        const bool prefer_software = (b == "sdl_sw");
        ren = sdl::create_renderer(win, prefer_software, prefer_software ? false : vsync);
        if (!ren) ren = sdl::create_renderer(win, !prefer_software, false); // fallback path
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

    bool read_pixels_rgba(uint8_t* out, sdl::ReadbackTiming* timing = nullptr) const {
        return sdl::read_pixels_rgba(ren, out, w, h, timing);
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
