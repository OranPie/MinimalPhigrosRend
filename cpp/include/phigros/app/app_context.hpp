#pragma once
// AppContext: all long-lived rendering/audio/input objects.
// Constructed once in main, passed by reference to GameLoop.
// Matches the former per-frame init block in main.cpp (lines ~265–375).

#include "phigros/app/window.hpp"
#include "phigros/app/input_manager.hpp"
#include "phigros/core/logger.hpp"
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
#include "phigros/chart/chart_loader.hpp"

#include <string>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>

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

    // Zip-aware texture cache for RPE line textures (texture_path field).
    // Key: path as it appears in the chart JSON (relative to chart root).
    std::unordered_map<std::string, render::Texture> line_tex_cache;
    std::string chart_dir;      // parent directory of chart file
    bool        chart_is_zip  = false;
    std::string chart_zip_file; // zip archive path when chart_is_zip

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
        window.init(W, H, "Phigros Renderer", headless, vsync, cfg.backend);
        PHLOG_INFO(Window, W << "x" << H
            << (headless ? " (headless)" : "")
            << (vsync ? "" : " (vsync off)")
            << " backend=" << window.normalize_backend(cfg.backend));
        PHLOG_DEBUG(Window, "render_driver=" << window.normalize_backend(cfg.backend)
            << " vsync=" << vsync << " headless=" << headless);

        batch.init(window.ren);
        draw_list.reserve(2048);

        // Resolve chart base for texture lookup
        if (chart::is_zip_path(chart_path)) {
            chart_is_zip  = true;
            chart_zip_file = chart::split_zip_path(chart_path).first;
        } else {
            chart_dir = fs::path(chart_path).parent_path().string();
        }

        // Respack
        std::string rp = respack_override.empty() ? cfg.respack_path : respack_override;
        respack = io::load_respack(window.ren, rp);
        PHLOG_INFO(Respack, (respack.loaded ? "Loaded" : "Fallback") << " (" << rp << ")");
        PHLOG_DEBUG(Respack, "color_perfect=(" << (int)respack.cfg.color_perfect.r
            << "," << (int)respack.cfg.color_perfect.g
            << "," << (int)respack.cfg.color_perfect.b
            << ") hitfx_duration=" << respack.cfg.hitfx_duration << "s");

        // Background
        std::string bgp = bg_override.empty() ? cfg.bg_path : bg_override;
        if (!bgp.empty()) {
            bg.load(window.ren, bgp, W, H, cfg.bg_blur);
            PHLOG_INFO(Render, "Background " << (bg.has_bg ? "loaded" : "failed") << ": " << bgp);
        }

        // Renderers
        line_ren.line_w = std::max(2.0, H * 0.005);
        line_ren.dot_r  = std::max(3.0, H * 0.007);
        note_ren.init(W, H, cfg.note_scale_x, cfg.note_scale_y);
        note_ren.note_outline = cfg.note_outline;
        hold_ren.init(W, H, cfg.note_scale_x, cfg.note_scale_y);
        trail.init(window.ren, W, H, cfg);
        motion_blur.init(window.ren, W, H, cfg);
        PHLOG_DEBUG(Render, "note_scale_x=" << cfg.note_scale_x
            << " note_scale_y=" << cfg.note_scale_y
            << " line_w=" << line_ren.line_w
            << " bg_dim=" << cfg.bg_dim);

        // Wire up zip-aware texture lookup for RPE line textures
        line_ren.texture_lookup = [this](const std::string& path) -> const render::Texture* {
            auto it = line_tex_cache.find(path);
            if (it != line_tex_cache.end())
                return it->second.valid() ? &it->second : nullptr;
            render::Texture tex;
            if (chart_is_zip) {
                auto data = chart::extract_zip_file(chart_zip_file, path);
                if (!data.empty())
                    tex = render::Texture::from_memory(window.ren, data.data(), (int)data.size());
            } else if (!chart_dir.empty()) {
                auto full = fs::path(chart_dir) / path;
                if (fs::exists(full))
                    tex = render::Texture::from_file(window.ren, full.string());
            }
            if (!tex.valid())
                PHLOG_WARN(Render, "LineTexture failed to load: " << path);
            line_tex_cache[path] = std::move(tex);
            return line_tex_cache[path].valid() ? &line_tex_cache[path] : nullptr;
        };

        // Wire up text draw for RPE textEvents (goes through SpriteBatch for DrawList compat)
        line_ren.text_draw = [this](const std::string& text, double x, double y,
                                    double rot, float sx, float sy,
                                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            if (!hud_ren.has_font) return;
            hud_ren.draw_text_rotated(batch, hud_ren.font_large, text,
                                      x, y, rot, sx, sy, r, g, b, a);
        };

        if (trail.enabled())
            PHLOG_INFO(Render, "Trail enabled: alpha=" << cfg.trail_alpha.value()
                << " frames=" << cfg.trail_frames.value_or(6)
                << " decay=" << cfg.trail_decay.value_or(0.85));
        if (motion_blur.enabled())
            PHLOG_INFO(Render, "MotionBlur enabled: samples=" << cfg.motion_blur_samples.value()
                << " shutter=" << cfg.motion_blur_shutter.value_or(0.5));

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
                if (has_audio)
                    PHLOG_INFO(Audio, "Loaded BGM: " << audio_path);
                else
                    PHLOG_WARN(Audio, "Failed to load BGM: " << audio_path);
            }
        } else {
            PHLOG_DEBUG(Audio, "No audio path found — will run silent");
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
        for (auto& [k, tex] : line_tex_cache) tex.destroy();
        line_tex_cache.clear();
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
