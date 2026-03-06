#pragma once
// AppContext: all long-lived rendering/audio/input objects.
// Constructed once in main, passed by reference to GameLoop.
// Matches the former per-frame init block in main.cpp (lines ~265–375).

#include "phigros/app/window.hpp"
#include "phigros/app/input_manager.hpp"
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/draw_list.hpp"
#include "phigros/render/background.hpp"
#include "phigros/render/line_renderer.hpp"
#include "phigros/render/note_renderer.hpp"
#include "phigros/render/hold_renderer.hpp"
#include "phigros/render/hitfx_renderer.hpp"
#include "phigros/render/hud_renderer.hpp"
#include "phigros/render/trail_renderer.hpp"
#include "phigros/render/motion_blur.hpp"
#include "phigros/render/sdl_executor.hpp"
#include "phigros/io/respack.hpp"
#include "phigros/io/audio.hpp"
#include "phigros/config/render_config.hpp"

#include <string>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace phigros::app {

// Find an audio file adjacent to the chart file.
inline std::string find_chart_audio(const std::string& chart_dir) {
    namespace fs = std::filesystem;
    for (const char* name : {"music.ogg","music.mp3","music.wav",
                              "bgm.ogg","bgm.mp3","bgm.wav"}) {
        auto p = fs::path(chart_dir) / name;
        if (fs::exists(p)) return p.string();
    }
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(chart_dir, ec)) {
        auto ext = entry.path().extension().string();
        if (ext == ".ogg" || ext == ".mp3" || ext == ".wav" || ext == ".flac")
            return entry.path().string();
    }
    return "";
}

struct AppContext {
    Window                     window;
    render::SpriteBatch        batch;
    render::DrawList           draw_list;
    io::Respack                respack;
    render::BackgroundRenderer bg;
    render::LineRenderer       line_ren;
    render::NoteRenderer       note_ren;
    render::HoldRenderer       hold_ren;
    render::HitFXRenderer      hitfx_ren;
    render::HudRenderer        hud_ren;
    render::TrailRenderer      trail;
    render::MotionBlurRenderer motion_blur;
    InputManager               input;
    io::AudioSystem            audio;

    bool        has_audio     = false;
    bool        started_audio = false;
    std::string audio_path;

    // Init all objects. chart_offset is passed to audio for seeking.
    void init(const std::string& chart_path,
              double             chart_offset,
              const std::string& respack_override,
              const std::string& bg_override,
              const std::string& font_path,
              const std::string& audio_override,
              bool               headless,
              int W, int H,
              const config::RenderConfig& cfg,
              bool               no_vsync = false)
    {
        namespace fs = std::filesystem;

        const bool vsync = !headless && !no_vsync;
        window.init(W, H, "Phigros Renderer", headless, vsync);
        std::cout << "[Window] " << W << "x" << H
                  << (headless ? " (headless)" : "")
                  << (vsync ? "" : " (vsync off)") << "\n";

        batch.init(window.ren);
        draw_list.reserve(2048);

        // Respack
        std::string rp = respack_override.empty() ? cfg.respack_path : respack_override;
        respack = io::load_respack(window.ren, rp);
        std::cout << "[Respack] " << (respack.loaded ? "Loaded" : "Fallback")
                  << " (" << rp << ")\n";

        // Background
        std::string bgp = bg_override.empty() ? cfg.bg_path : bg_override;
        if (!bgp.empty()) {
            bg.load(window.ren, bgp, W, H, cfg.bg_blur);
            std::cout << "[Background] " << (bg.has_bg ? "Loaded" : "Failed") << "\n";
        }

        // Renderers
        line_ren.line_w = std::max(2.0, H * 0.005);
        line_ren.dot_r  = std::max(3.0, H * 0.007);
        note_ren.init(W, H, cfg.note_scale_x, cfg.note_scale_y);
        note_ren.note_outline = cfg.note_outline;
        hold_ren.init(W, H, cfg.note_scale_x, cfg.note_scale_y);
        trail.init(window.ren, W, H, cfg);
        motion_blur.init(window.ren, W, H, cfg);

        if (trail.enabled())
            std::cout << "[Trail] alpha=" << cfg.trail_alpha.value()
                      << " frames=" << cfg.trail_frames.value_or(6)
                      << " decay=" << cfg.trail_decay.value_or(0.85) << "\n";
        if (motion_blur.enabled())
            std::cout << "[MotionBlur] samples=" << cfg.motion_blur_samples.value()
                      << " shutter=" << cfg.motion_blur_shutter.value_or(0.5) << "\n";

        hud_ren.init(window.ren, font_path, W, H);
        hud_ren.screen_w = W;
        hud_ren.screen_h = H;

        // Audio
        audio_path = audio_override;
        if (audio_path.empty())
            audio_path = find_chart_audio(fs::path(chart_path).parent_path().string());
        if (!audio_path.empty()) {
            if (audio.init()) {
                has_audio = audio.load_bgm(audio_path, chart_offset);
                if (has_audio) std::cout << "[Audio] Loaded: " << audio_path << "\n";
            }
        }
    }

    // Reload audio from scratch (used on R-restart in play mode).
    void reload_audio(double chart_offset) {
        if (!audio_path.empty()) {
            audio.destroy();
            audio.init();
            has_audio     = audio.load_bgm(audio_path, chart_offset);
            started_audio = false;
        }
    }

    void destroy() {
        trail.destroy();
        motion_blur.destroy();
        audio.destroy();
        respack.destroy();
        bg.destroy();
        hud_ren.destroy();
        window.destroy();
    }
};

} // namespace phigros::app
