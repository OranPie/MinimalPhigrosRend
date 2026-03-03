#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <stdexcept>
#include <functional>

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
        Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER;
        if (SDL_Init(flags) != 0)
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

        Uint32 wflags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
        if (headless) wflags = SDL_WINDOW_HIDDEN;

        win = SDL_CreateWindow(title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            w, h, wflags);
        if (!win) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

        Uint32 rflags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
        ren = SDL_CreateRenderer(win, -1, rflags);
        if (!ren) {
            // Fallback to software renderer (headless)
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!ren) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_RenderSetLogicalSize(ren, w, h);
    }

    void poll_events() {
        SDL_Event e;
        resized = false;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit_requested = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                quit_requested = true;
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                w = e.window.data1;
                h = e.window.data2;
                resized = true;
            }
        }
    }

    void begin_frame() {
        SDL_SetRenderDrawColor(ren, 10, 10, 14, 255);
        SDL_RenderClear(ren);
    }

    void end_frame() {
        SDL_RenderPresent(ren);
    }

    // Save current framebuffer to BMP file
    bool save_screenshot(const std::string& path) const {
        SDL_Surface* sshot = SDL_CreateRGBSurfaceWithFormat(
            0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!sshot) return false;
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                             sshot->pixels, sshot->pitch);
        int ret = SDL_SaveBMP(sshot, path.c_str());
        SDL_FreeSurface(sshot);
        return ret == 0;
    }

    void destroy() {
        if (ren) { SDL_DestroyRenderer(ren); ren = nullptr; }
        if (win) { SDL_DestroyWindow(win); win = nullptr; }
        SDL_Quit();
    }

    // High-resolution timer
    static double get_time_sec() {
        return static_cast<double>(SDL_GetPerformanceCounter()) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    }
};

} // namespace phigros::app
