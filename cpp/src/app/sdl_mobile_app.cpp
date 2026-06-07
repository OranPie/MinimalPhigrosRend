#include "phigros/app/sdl_compat.hpp"
#include "phigros/chart/chart_loader.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/core/logger.hpp"
#include "phigros/engine/exact_autoplay.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/render/background.hpp"
#include "phigros/render/draw_list.hpp"
#include "phigros/render/hitfx_renderer.hpp"
#include "phigros/render/hold_renderer.hpp"
#include "phigros/render/hud_renderer.hpp"
#include "phigros/render/line_renderer.hpp"
#include "phigros/render/note_renderer.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/render/sdl_executor.hpp"
#include "phigros/render/sprite_batch.hpp"
#include "phigros/io/respack.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using phigros::ChartData;
using phigros::NoteState;
using phigros::chart::ChartEntry;
using phigros::config::RenderConfig;
using phigros::engine::Judge;
using phigros::engine::ScoreResult;
using phigros::render::Texture;

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    bool contains(double px, double py) const {
        return px >= x && py >= y && px <= x + w && py <= y + h;
    }
};

enum class Screen {
    Library,
    Detail,
    Settings,
    Playing,
    Paused,
    Result,
};

struct PlayingChart {
    ChartData chart;
    ChartEntry entry;
    std::vector<NoteState> states;
    Judge judge;
    phigros::engine::EffectManager effects;
    double t = 0.0;
    double prev_t = 0.0;
    double chart_end = 0.0;
    double last_wall = 0.0;
    bool loaded = false;
    bool finished = false;
};

class SdlMobileApp {
public:
    int run() {
        if (!phigros::app::sdl::sdl_init()) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        window_ = phigros::app::sdl::create_window("Phigros Renderer", width_, height_, false);
        if (!window_) {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return 1;
        }
        renderer_ = phigros::app::sdl::create_renderer(window_, false, true);
        if (!renderer_) {
            renderer_ = phigros::app::sdl::create_renderer(window_, true, false);
        }
        if (!renderer_) {
            std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        phigros::app::sdl::set_render_logical_size(renderer_, width_, height_);

        batch_.init(renderer_);
        load_font();
        load_respack();
        scan_library();

        last_frame_wall_ = now_sec();
        while (running_) {
            poll_events();
            update();
            draw();
            SDL_Delay(1);
        }

        destroy();
        return 0;
    }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int width_ = 1280;
    int height_ = 720;
    bool running_ = true;
    Screen screen_ = Screen::Library;
    Screen before_pause_ = Screen::Library;

    phigros::render::SpriteBatch batch_;
    phigros::render::DrawList draw_list_;
    phigros::render::LineRenderer line_renderer_;
    phigros::render::NoteRenderer note_renderer_;
    phigros::render::HoldRenderer hold_renderer_;
    phigros::render::HitFXRenderer hitfx_renderer_;
    phigros::render::HudRenderer hud_;
    phigros::render::BackgroundRenderer bg_;
    phigros::io::Respack respack_;
    RenderConfig cfg_;

    std::vector<ChartEntry> library_;
    int selected_ = -1;
    int library_page_ = 0;
    std::string status_;
    bool autoplay_ = true;
    bool show_particles_ = true;
    bool show_hitfx_ = true;
    double last_frame_wall_ = 0.0;
    double fps_ = 60.0;

    std::unique_ptr<PlayingChart> playing_;
    phigros::engine::ScoreResult last_score_{};

    std::unordered_map<std::string, Texture> line_texture_cache_;
    std::string chart_dir_;
    bool chart_is_zip_ = false;
    std::string chart_zip_file_;

    static double now_sec() {
        return static_cast<double>(SDL_GetPerformanceCounter()) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    }

    void destroy() {
        for (auto& item : line_texture_cache_) item.second.destroy();
        line_texture_cache_.clear();
        bg_.destroy();
        respack_.destroy();
        hud_.destroy();
        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }

    std::string base_path() const {
#if defined(PHIGROS_SDL3)
        const char* raw = SDL_GetBasePath();
        if (!raw) return {};
        return std::string(raw);
#else
        char* raw = SDL_GetBasePath();
        if (!raw) return {};
        std::string out(raw);
        SDL_free(raw);
        return out;
#endif
    }

    std::vector<fs::path> search_roots() const {
        std::vector<fs::path> roots;
        std::error_code ec;
        roots.push_back(fs::current_path(ec));
        if (!base_path().empty()) roots.emplace_back(base_path());
        if (!roots.empty()) roots.push_back(roots.front() / "charts");
        if (!roots.empty()) roots.push_back(roots.front() / ".." / "charts");
#if defined(PHIGROS_ANDROID)
        roots.emplace_back("/sdcard/phigros");
        roots.emplace_back("/sdcard/Phigros");
#endif
        return roots;
    }

    std::string find_existing_file(std::initializer_list<std::string> names) const {
        for (const auto& root : search_roots()) {
            for (const auto& name : names) {
                fs::path p = root / name;
                std::error_code ec;
                if (fs::is_regular_file(p, ec)) return p.string();
            }
        }
        return {};
    }

    void load_font() {
        const std::string font_path = find_existing_file({
            "assets/cmdysj.ttf",
            "../assets/cmdysj.ttf",
            "cmdysj.ttf",
        });
        hud_.init(renderer_, font_path, width_, height_, 1.0, true, false);
    }

    void load_respack() {
        cfg_.window_w = width_;
        cfg_.window_h = height_;
        cfg_.backend = "sdl";
        cfg_.show_particles = show_particles_;
        cfg_.show_hitfx = show_hitfx_;
        cfg_.respack_path = find_existing_file({"respack.zip", "../respack.zip"});
        if (cfg_.respack_path.empty()) cfg_.respack_path = "./respack.zip";
        respack_ = phigros::io::load_respack(renderer_, cfg_.respack_path);
        note_renderer_.init(width_, height_, cfg_.note_scale_x, cfg_.note_scale_y);
        note_renderer_.note_outline = cfg_.note_outline;
        hold_renderer_.init(width_, height_, cfg_.note_scale_x, cfg_.note_scale_y);
        line_renderer_.line_w = std::max(2.0, height_ * 0.005);
        line_renderer_.dot_r = std::max(3.0, height_ * 0.007);
        line_renderer_.texture_lookup = [this](const std::string& path) -> const Texture* {
            return load_line_texture(path);
        };
        line_renderer_.text_draw = [this](const std::string& text, double x, double y,
                                          double rot, float sx, float sy,
                                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            if (!hud_.has_font) return;
            hud_.draw_text_rotated(batch_, hud_.font_large, text, x, y, rot, sx, sy, r, g, b, a);
        };
    }

    void scan_library() {
        library_.clear();
        status_.clear();
        std::vector<std::string> seen;
        for (const auto& root : search_roots()) {
            std::error_code ec;
            if (!fs::is_directory(root, ec)) continue;
            auto entries = phigros::chart::scan_charts_directory(root.string());
            for (auto& entry : entries) {
                if (entry.chart_path.empty()) continue;
                if (std::find(seen.begin(), seen.end(), entry.chart_path) != seen.end()) continue;
                seen.push_back(entry.chart_path);
                library_.push_back(std::move(entry));
            }
        }
        std::stable_sort(library_.begin(), library_.end(), [](const ChartEntry& a, const ChartEntry& b) {
            if (a.name != b.name) return a.name < b.name;
            return a.difficulty < b.difficulty;
        });
        selected_ = library_.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(library_.size()) - 1);
        if (library_.empty()) {
            status_ = "No charts found. Put chart folders or zips under charts/.";
        } else {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%zu chart entries found.", library_.size());
            status_ = buf;
        }
    }

    void poll_events() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (phigros::app::sdl::handle_event_quit(e)) {
                running_ = false;
                continue;
            }
            if (phigros::app::sdl::handle_event_window_resized(e, width_, height_)) {
                on_resize();
                continue;
            }
            if (e.type == PHIGROS_SDL_EVENT_KEY_DOWN) {
                const auto sc = PHIGROS_KEY_SCANCODE(e);
                if (sc == SDL_SCANCODE_ESCAPE) {
                    back();
                } else if (sc == SDL_SCANCODE_SPACE && (screen_ == Screen::Playing || screen_ == Screen::Paused)) {
                    toggle_pause();
                } else if (sc == SDL_SCANCODE_R && playing_) {
                    start_playing(selected_);
                }
                continue;
            }
            if (e.type == PHIGROS_SDL_MOUSE_UP) {
                handle_tap(PHIGROS_MOUSE_X(e), PHIGROS_MOUSE_Y(e));
                continue;
            }
            if (e.type == PHIGROS_SDL_FINGER_UP) {
                handle_tap(e.tfinger.x * width_, e.tfinger.y * height_);
                continue;
            }
        }
    }

    void on_resize() {
        cfg_.window_w = width_;
        cfg_.window_h = height_;
        phigros::app::sdl::set_render_logical_size(renderer_, width_, height_);
        hud_.screen_w = width_;
        hud_.screen_h = height_;
        note_renderer_.init(width_, height_, cfg_.note_scale_x, cfg_.note_scale_y);
        hold_renderer_.init(width_, height_, cfg_.note_scale_x, cfg_.note_scale_y);
        line_renderer_.line_w = std::max(2.0, height_ * 0.005);
        line_renderer_.dot_r = std::max(3.0, height_ * 0.007);
        if (playing_ && playing_->loaded) {
            prepare_chart_render_base(playing_->entry);
        }
    }

    void update() {
        const double now = now_sec();
        const double dt = std::clamp(now - last_frame_wall_, 0.0, 0.1);
        if (dt > 0.0) fps_ = fps_ * 0.9 + (1.0 / dt) * 0.1;
        last_frame_wall_ = now;

        if (screen_ == Screen::Playing && playing_ && playing_->loaded) {
            update_playing(now);
        }
    }

    void update_playing(double now) {
        auto& p = *playing_;
        double dt = std::clamp(now - p.last_wall, 0.0, 0.1);
        p.last_wall = now;
        p.prev_t = p.t;
        p.t += dt * cfg_.chart_speed;

        static thread_local std::vector<phigros::engine::SimHitEvent> hit_events;
        hit_events.clear();
        if (autoplay_) {
            phigros::engine::exact_autoplay_step(
                p.prev_t, p.t, p.chart.notes, p.states, p.chart.lines, p.judge,
                width_, height_, &hit_events);
            for (const auto& ev : hit_events) add_hit_effect(ev);
        }

        const int idx_next = find_idx_next(p, p.t);
        phigros::engine::detect_misses(p.states, idx_next, p.t, Judge::BAD, p.judge);
        phigros::engine::hold_maintenance(p.states, idx_next, p.t, cfg_.hold_tail_tol, p.judge);
        phigros::engine::hold_finalize(p.states, idx_next, p.t, cfg_.hold_tail_tol, Judge::BAD, p.judge);
        p.effects.hold_tick_fx(p.states, idx_next, p.t, cfg_.hold_fx_interval_ms,
                               p.chart.lines, respack_.cfg.color_perfect,
                               respack_.cfg.alpha_perfect, "perfect");
        p.effects.update(p.t, p.t * 1000.0, respack_.cfg.hitfx_duration);

        if (!p.finished && p.t >= p.chart_end) {
            p.finished = true;
            last_score_ = phigros::engine::compute_score(
                p.judge.acc_sum, p.judge.max_combo, p.chart.playable_count);
            screen_ = Screen::Result;
        }
    }

    int find_idx_next(const PlayingChart& p, double t) const {
        int lo = 0;
        int hi = static_cast<int>(p.states.size());
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (p.states[mid].judged || p.states[mid].note->t_hit < t - 0.5)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    void add_hit_effect(const phigros::engine::SimHitEvent& ev) {
        if (!playing_ || ev.note_idx < 0 || ev.note_idx >= static_cast<int>(playing_->chart.notes.size()))
            return;
        const auto& note = playing_->chart.notes[ev.note_idx];
        phigros::math::RGB color = note.tint_hitfx_rgb.value_or(
            ev.grade == "GOOD" || ev.grade == "BAD"
                ? respack_.cfg.color_good
                : respack_.cfg.color_perfect);
        const uint8_t alpha = (ev.grade == "GOOD" || ev.grade == "BAD")
            ? respack_.cfg.alpha_good
            : respack_.cfg.alpha_perfect;
        const std::string variant = (ev.grade == "GOOD" || ev.grade == "BAD") ? "good" : "perfect";
        playing_->effects.add_hitfx(ev.x, ev.y, ev.judge_t, color, 0.0, 0.0, variant, alpha);
        if (show_particles_) {
            playing_->effects.add_particle_burst(
                ev.x, ev.y, ev.judge_t * 1000.0,
                respack_.cfg.hitfx_duration * 1000.0, color);
        }
    }

    void draw() {
        phigros::app::sdl::set_draw_color(renderer_, 10, 12, 16, 255);
        SDL_RenderClear(renderer_);
        switch (screen_) {
            case Screen::Library: draw_library(); break;
            case Screen::Detail: draw_detail(); break;
            case Screen::Settings: draw_settings(); break;
            case Screen::Playing: draw_playing(false); break;
            case Screen::Paused: draw_playing(true); draw_pause_menu(); break;
            case Screen::Result: draw_playing(false); draw_result(); break;
        }
        SDL_RenderPresent(renderer_);
    }

    void draw_text(const std::string& text, double x, double y,
                   uint8_t r = 235, uint8_t g = 240, uint8_t b = 245, uint8_t a = 235,
                   bool large = false) const {
        if (!hud_.has_font) return;
        hud_.draw_text(batch_, large ? hud_.font_large : hud_.font_small, text, x, y, r, g, b, a);
    }

    void draw_button(const Rect& rect, const std::string& label, bool enabled = true) const {
        const uint8_t base = enabled ? 42 : 28;
        batch_.draw_rect(rect.x, rect.y, rect.w, rect.h, base, base + 5, base + 12, 235);
        batch_.draw_line(rect.x, rect.y, rect.x + rect.w, rect.y, 1.0, 95, 110, 130, 210);
        batch_.draw_line(rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0, 22, 24, 28, 210);
        if (hud_.has_font) {
            const auto& font = hud_.font_small;
            double tw = hud_.text_width(font, label);
            double th = hud_.text_line_height(font);
            draw_text(label, rect.x + (rect.w - tw) * 0.5, rect.y + (rect.h - th) * 0.5,
                      enabled ? 238 : 120, enabled ? 242 : 128, enabled ? 248 : 136, 235);
        }
    }

    void draw_top_bar(const std::string& title) {
        batch_.draw_rect(0, 0, width_, 64, 15, 17, 22, 245);
        draw_text(title, 24, 18, 245, 248, 252, 245, true);
        char fps_buf[48];
        std::snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS", fps_);
        if (hud_.has_font) {
            const double tw = hud_.text_width(hud_.font_small, fps_buf);
            draw_text(fps_buf, width_ - tw - 24, 22, 150, 170, 190, 220);
        }
    }

    void draw_library() {
        draw_top_bar("Phigros Renderer");
        const double pad = 24.0;
        const Rect settings{width_ - 224.0, 84.0, 92.0, 44.0};
        const Rect rescan{width_ - 120.0, 84.0, 96.0, 44.0};
        draw_button(settings, "Settings");
        draw_button(rescan, "Rescan");
        draw_text(status_, pad, 88, 170, 190, 210, 230);

        const int rows = std::max(1, static_cast<int>((height_ - 190) / 58));
        const int max_page = library_.empty() ? 0 : (static_cast<int>(library_.size()) - 1) / rows;
        library_page_ = std::clamp(library_page_, 0, max_page);
        const int start = library_page_ * rows;
        const int end = std::min(static_cast<int>(library_.size()), start + rows);

        double y = 150.0;
        for (int i = start; i < end; ++i) {
            const Rect row{pad, y, width_ - pad * 2.0, 48.0};
            const bool chosen = i == selected_;
            batch_.draw_rect(row.x, row.y, row.w, row.h,
                             chosen ? 58 : 28, chosen ? 70 : 34, chosen ? 86 : 44, 235);
            batch_.draw_rect(row.x, row.y, 4.0, row.h,
                             chosen ? 115 : 62, chosen ? 210 : 150, chosen ? 255 : 185, 235);
            const auto& entry = library_[i];
            std::string name = entry.name.empty() ? fs::path(entry.chart_path).stem().string() : entry.name;
            std::string line = name;
            if (!entry.difficulty.empty()) line += "  [" + entry.difficulty + "]";
            if (entry.format != phigros::chart::ChartFormat::Unknown)
                line += "  " + phigros::chart::chart_format_name(entry.format);
            draw_text(line, row.x + 16.0, row.y + 12.0, 235, 240, 245, 235);
            y += 58.0;
        }

        const Rect prev{pad, height_ - 62.0, 100.0, 42.0};
        const Rect next{pad + 112.0, height_ - 62.0, 100.0, 42.0};
        draw_button(prev, "Prev", library_page_ > 0);
        draw_button(next, "Next", library_page_ < max_page);
        char page_buf[64];
        std::snprintf(page_buf, sizeof(page_buf), "Page %d / %d", library_page_ + 1, max_page + 1);
        draw_text(page_buf, pad + 230.0, height_ - 52.0, 160, 178, 196, 220);
    }

    void draw_detail() {
        draw_top_bar("Chart Detail");
        if (selected_ < 0 || selected_ >= static_cast<int>(library_.size())) {
            draw_text("No chart selected.", 32, 100);
            return;
        }
        const auto& e = library_[selected_];
        const Rect back{24, 84, 96, 44};
        const Rect play{width_ - 148.0, 84, 124, 44};
        draw_button(back, "Back");
        draw_button(play, "Play");

        const double x = 32.0;
        double y = 160.0;
        const std::string name = e.name.empty() ? fs::path(e.chart_path).stem().string() : e.name;
        draw_text(name, x, y, 248, 250, 255, 245, true);
        y += 58.0;
        draw_text("Difficulty: " + (e.difficulty.empty() ? std::string("-") : e.difficulty), x, y); y += 34.0;
        draw_text("Format: " + phigros::chart::chart_format_name(e.format), x, y); y += 34.0;
        draw_text("Source: " + e.source_type, x, y); y += 34.0;
        draw_text("Chart: " + compact_path(e.chart_path), x, y); y += 34.0;
        draw_text("Music: " + (e.assets.music_path.empty() ? std::string("-") : compact_path(e.assets.music_path)), x, y); y += 34.0;
        draw_text("Image: " + (e.assets.illustration_path.empty() ? std::string("-") : compact_path(e.assets.illustration_path)), x, y); y += 52.0;

        const Rect mode{x, y, 170.0, 44.0};
        draw_button(mode, autoplay_ ? "Autoplay" : "Manual");
        draw_text("Manual input is owned by the SDL app shell and will share this page.", x + 190.0, y + 12.0,
                  150, 170, 190, 220);
    }

    void draw_settings() {
        draw_top_bar("Settings");
        const Rect back{24, 84, 96, 44};
        draw_button(back, "Back");
        double x = 36.0;
        double y = 160.0;
        draw_setting_toggle("Autoplay", autoplay_, x, y); y += 64.0;
        draw_setting_toggle("Hit FX", show_hitfx_, x, y); y += 64.0;
        draw_setting_toggle("Particles", show_particles_, x, y); y += 76.0;
        draw_slider("Chart speed", cfg_.chart_speed, 0.5, 3.0, x, y); y += 76.0;
        draw_slider("Background dim", cfg_.bg_dim, 0.0, 220.0, x, y);
    }

    void draw_setting_toggle(const std::string& label, bool value, double x, double y) {
        draw_text(label, x, y + 10.0);
        const Rect box{x + 210.0, y, 110.0, 44.0};
        draw_button(box, value ? "On" : "Off");
    }

    void draw_slider(const std::string& label, double value, double min_v, double max_v, double x, double y) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  %.2f", label.c_str(), value);
        draw_text(buf, x, y);
        const double sx = x + 210.0;
        const double sy = y + 10.0;
        const double sw = std::min(430.0, width_ - sx - 36.0);
        batch_.draw_rect(sx, sy + 12.0, sw, 6.0, 45, 50, 58, 255);
        double p = std::clamp((value - min_v) / std::max(0.0001, max_v - min_v), 0.0, 1.0);
        batch_.draw_rect(sx, sy + 12.0, sw * p, 6.0, 105, 205, 245, 255);
        batch_.draw_rect(sx + sw * p - 7.0, sy, 14.0, 30.0, 235, 240, 245, 255);
    }

    void draw_playing(bool frozen) {
        if (!playing_ || !playing_->loaded) {
            draw_top_bar("Loading");
            draw_text(status_.empty() ? "No chart loaded." : status_, 32, 110);
            return;
        }
        auto& p = *playing_;
        auto frame = phigros::render::build_frame(p.t, p.chart, p.states, p.judge, cfg_);

        if (bg_.has_bg) {
            bg_.draw(batch_, cfg_.bg_dim);
        } else {
            batch_.draw_rect(0, 0, width_, height_, 8, 10, 14, 255);
        }

        draw_list_.clear();
        draw_list_.cmds.reserve(1024);
        batch_.dl = &draw_list_;
        hold_renderer_.draw(batch_, respack_, frame.notes, p.t, width_, height_, cfg_.expand_factor);
        line_renderer_.draw(batch_, respack_.white_tex, frame.lines, width_, height_, cfg_.expand_factor, false);
        note_renderer_.draw(batch_, respack_, frame.notes, p.t, width_, height_, cfg_.expand_factor);
        line_renderer_.draw(batch_, respack_.white_tex, frame.lines, width_, height_, cfg_.expand_factor, true);
        hitfx_renderer_.draw(batch_, respack_, p.effects, p.t,
                             show_hitfx_, show_particles_, static_cast<float>(cfg_.hitfx_intensity),
                             width_, height_, cfg_.expand_factor);
        batch_.dl = nullptr;
        phigros::render::SdlExecutor::execute(renderer_, draw_list_);

        hud_.draw(batch_, frame.hud, fps_);
        const Rect pause{20, height_ - 58.0, 96.0, 42.0};
        draw_button(pause, frozen ? "Paused" : "Pause");
    }

    void draw_pause_menu() {
        batch_.draw_rect(0, 0, width_, height_, 0, 0, 0, 130);
        const double panel_w = 320.0;
        const double panel_h = 242.0;
        const double x = (width_ - panel_w) * 0.5;
        const double y = (height_ - panel_h) * 0.5;
        batch_.draw_rect(x, y, panel_w, panel_h, 18, 22, 29, 245);
        draw_text("Paused", x + 24.0, y + 24.0, 245, 248, 252, 245, true);
        draw_button({x + 24.0, y + 86.0, panel_w - 48.0, 42.0}, "Resume");
        draw_button({x + 24.0, y + 138.0, panel_w - 48.0, 42.0}, "Restart");
        draw_button({x + 24.0, y + 190.0, panel_w - 48.0, 42.0}, "Library");
    }

    void draw_result() {
        batch_.draw_rect(0, 0, width_, height_, 0, 0, 0, 145);
        const double panel_w = 420.0;
        const double panel_h = 286.0;
        const double x = (width_ - panel_w) * 0.5;
        const double y = (height_ - panel_h) * 0.5;
        batch_.draw_rect(x, y, panel_w, panel_h, 18, 22, 29, 248);
        draw_text("Result", x + 26.0, y + 24.0, 245, 248, 252, 245, true);
        char score[96];
        std::snprintf(score, sizeof(score), "Score  %d", last_score_.score);
        char acc[96];
        std::snprintf(acc, sizeof(acc), "Accuracy  %.2f%%", last_score_.acc_ratio * 100.0);
        char combo[96];
        std::snprintf(combo, sizeof(combo), "Max combo  %d", playing_ ? playing_->judge.max_combo : 0);
        draw_text(score, x + 32.0, y + 92.0);
        draw_text(acc, x + 32.0, y + 126.0);
        draw_text(combo, x + 32.0, y + 160.0);
        draw_button({x + 32.0, y + 214.0, 150.0, 44.0}, "Replay");
        draw_button({x + 198.0, y + 214.0, 190.0, 44.0}, "Library");
    }

    static std::string compact_path(const std::string& path) {
        if (path.size() <= 86) return path;
        return path.substr(0, 36) + "..." + path.substr(path.size() - 46);
    }

    void handle_tap(double x, double y) {
        switch (screen_) {
            case Screen::Library: handle_library_tap(x, y); break;
            case Screen::Detail: handle_detail_tap(x, y); break;
            case Screen::Settings: handle_settings_tap(x, y); break;
            case Screen::Playing:
                if (Rect{20, height_ - 58.0, 96.0, 42.0}.contains(x, y)) toggle_pause();
                break;
            case Screen::Paused: handle_pause_tap(x, y); break;
            case Screen::Result: handle_result_tap(x, y); break;
        }
    }

    void handle_library_tap(double x, double y) {
        if (Rect{width_ - 224.0, 84.0, 92.0, 44.0}.contains(x, y)) {
            screen_ = Screen::Settings;
            return;
        }
        if (Rect{width_ - 120.0, 84.0, 96.0, 44.0}.contains(x, y)) {
            scan_library();
            return;
        }
        const int rows = std::max(1, static_cast<int>((height_ - 190) / 58));
        const int max_page = library_.empty() ? 0 : (static_cast<int>(library_.size()) - 1) / rows;
        if (Rect{24, height_ - 62.0, 100.0, 42.0}.contains(x, y) && library_page_ > 0) {
            --library_page_;
            return;
        }
        if (Rect{136, height_ - 62.0, 100.0, 42.0}.contains(x, y) && library_page_ < max_page) {
            ++library_page_;
            return;
        }
        const int start = library_page_ * rows;
        for (int row = 0; row < rows; ++row) {
            int idx = start + row;
            if (idx >= static_cast<int>(library_.size())) break;
            if (Rect{24, 150.0 + row * 58.0, width_ - 48.0, 48.0}.contains(x, y)) {
                selected_ = idx;
                screen_ = Screen::Detail;
                return;
            }
        }
    }

    void handle_detail_tap(double x, double y) {
        if (Rect{24, 84, 96, 44}.contains(x, y)) {
            screen_ = Screen::Library;
            return;
        }
        if (Rect{width_ - 148.0, 84, 124, 44}.contains(x, y)) {
            start_playing(selected_);
            return;
        }
        if (Rect{32.0, 160.0 + 58.0 + 34.0 * 5.0 + 18.0, 170.0, 44.0}.contains(x, y)) {
            autoplay_ = !autoplay_;
        }
    }

    void handle_settings_tap(double x, double y) {
        if (Rect{24, 84, 96, 44}.contains(x, y)) {
            screen_ = Screen::Library;
            return;
        }
        double base_y = 160.0;
        if (Rect{36.0 + 210.0, base_y, 110.0, 44.0}.contains(x, y)) autoplay_ = !autoplay_;
        if (Rect{36.0 + 210.0, base_y + 64.0, 110.0, 44.0}.contains(x, y)) show_hitfx_ = !show_hitfx_;
        if (Rect{36.0 + 210.0, base_y + 128.0, 110.0, 44.0}.contains(x, y)) show_particles_ = !show_particles_;

        adjust_slider(x, y, base_y + 204.0, cfg_.chart_speed, 0.5, 3.0);
        double bg_dim = static_cast<double>(cfg_.bg_dim);
        if (adjust_slider(x, y, base_y + 280.0, bg_dim, 0.0, 220.0))
            cfg_.bg_dim = static_cast<int>(std::round(bg_dim));
    }

    bool adjust_slider(double x, double y, double row_y, double& value, double min_v, double max_v) {
        const double sx = 36.0 + 210.0;
        const double sy = row_y + 10.0;
        const double sw = std::min(430.0, width_ - sx - 36.0);
        if (!Rect{sx - 10.0, sy - 8.0, sw + 20.0, 46.0}.contains(x, y)) return false;
        const double p = std::clamp((x - sx) / std::max(1.0, sw), 0.0, 1.0);
        value = min_v + p * (max_v - min_v);
        return true;
    }

    void handle_pause_tap(double x, double y) {
        const double panel_w = 320.0;
        const double panel_h = 242.0;
        const double px = (width_ - panel_w) * 0.5;
        const double py = (height_ - panel_h) * 0.5;
        if (Rect{px + 24.0, py + 86.0, panel_w - 48.0, 42.0}.contains(x, y)) {
            toggle_pause();
        } else if (Rect{px + 24.0, py + 138.0, panel_w - 48.0, 42.0}.contains(x, y)) {
            start_playing(selected_);
        } else if (Rect{px + 24.0, py + 190.0, panel_w - 48.0, 42.0}.contains(x, y)) {
            playing_.reset();
            screen_ = Screen::Library;
        }
    }

    void handle_result_tap(double x, double y) {
        const double panel_w = 420.0;
        const double panel_h = 286.0;
        const double px = (width_ - panel_w) * 0.5;
        const double py = (height_ - panel_h) * 0.5;
        if (Rect{px + 32.0, py + 214.0, 150.0, 44.0}.contains(x, y)) {
            start_playing(selected_);
        } else if (Rect{px + 198.0, py + 214.0, 190.0, 44.0}.contains(x, y)) {
            playing_.reset();
            screen_ = Screen::Library;
        }
    }

    void back() {
        if (screen_ == Screen::Library) {
            running_ = false;
        } else if (screen_ == Screen::Playing) {
            toggle_pause();
        } else if (screen_ == Screen::Paused) {
            screen_ = before_pause_;
            if (playing_) playing_->last_wall = now_sec();
        } else if (screen_ == Screen::Result) {
            playing_.reset();
            screen_ = Screen::Library;
        } else {
            screen_ = Screen::Library;
        }
    }

    void toggle_pause() {
        if (screen_ == Screen::Playing) {
            before_pause_ = Screen::Playing;
            screen_ = Screen::Paused;
        } else if (screen_ == Screen::Paused) {
            screen_ = before_pause_;
            if (playing_) playing_->last_wall = now_sec();
        }
    }

    void start_playing(int index) {
        if (index < 0 || index >= static_cast<int>(library_.size())) return;
        selected_ = index;
        status_ = "Loading chart...";
        playing_ = std::make_unique<PlayingChart>();
        try {
            cfg_.window_w = width_;
            cfg_.window_h = height_;
            cfg_.show_hitfx = show_hitfx_;
            cfg_.show_particles = show_particles_;
            auto loaded = phigros::chart::load_chart_with_entry(
                library_[index].chart_path, width_, height_, cfg_.rpe_easing_shift);
            playing_->chart = std::move(loaded.chart);
            playing_->entry = std::move(loaded.entry);
            if (!playing_->chart.is_compiled) {
                phigros::engine::precompute_t_enter(
                    playing_->chart.lines, playing_->chart.notes,
                    width_, height_, cfg_.expand_factor,
                    cfg_.note_scale_x, cfg_.note_scale_y);
            }
            playing_->chart.build_notes_by_enter_index();
            playing_->states.resize(playing_->chart.notes.size());
            for (size_t i = 0; i < playing_->states.size(); ++i)
                playing_->states[i].note = &playing_->chart.notes[i];
            playing_->effects.particle_count = cfg_.particle_count;
            playing_->chart_end = playing_->chart.chart_end_t + 2.0;
            playing_->t = playing_->chart.offset - 1.0;
            playing_->prev_t = playing_->t;
            playing_->last_wall = now_sec();
            playing_->loaded = true;
            prepare_chart_render_base(playing_->entry);
            screen_ = Screen::Playing;
            status_.clear();
        } catch (const std::exception& e) {
            status_ = std::string("Chart load failed: ") + e.what();
            playing_.reset();
            screen_ = Screen::Detail;
        }
    }

    void prepare_chart_render_base(const ChartEntry& entry) {
        bg_.destroy();
        chart_dir_.clear();
        chart_is_zip_ = false;
        chart_zip_file_.clear();
        for (auto& item : line_texture_cache_) item.second.destroy();
        line_texture_cache_.clear();

        if (phigros::chart::is_zip_path(entry.chart_path)) {
            chart_is_zip_ = true;
            chart_zip_file_ = phigros::chart::split_zip_path(entry.chart_path).first;
        } else {
            chart_dir_ = fs::path(entry.chart_path).parent_path().string();
        }

        std::string bg_path = entry.assets.illustration_path;
        if (bg_path.empty() && playing_) {
            if (!playing_->chart.metadata.bg_path.empty()) {
                if (chart_is_zip_) bg_path = chart_zip_file_ + ":" + playing_->chart.metadata.bg_path;
                else bg_path = (fs::path(chart_dir_) / playing_->chart.metadata.bg_path).string();
            }
        }
        if (!bg_path.empty()) {
            if (phigros::chart::is_zip_path(bg_path)) {
                auto [zip_file, member] = phigros::chart::split_zip_path(bg_path);
                auto bytes = phigros::chart::extract_zip_file(zip_file, member);
                if (!bytes.empty())
                    bg_.load_from_memory(renderer_, bytes.data(), static_cast<int>(bytes.size()), width_, height_);
            } else {
                bg_.load(renderer_, bg_path, width_, height_, cfg_.bg_blur);
            }
        }
    }

    const Texture* load_line_texture(const std::string& path) {
        auto it = line_texture_cache_.find(path);
        if (it != line_texture_cache_.end()) return it->second.valid() ? &it->second : nullptr;

        Texture tex;
        if (chart_is_zip_) {
            auto bytes = phigros::chart::extract_zip_file(chart_zip_file_, path);
            if (!bytes.empty()) {
                tex = Texture::from_memory(renderer_, bytes.data(), static_cast<int>(bytes.size()));
            }
        } else if (!chart_dir_.empty()) {
            fs::path full = fs::path(chart_dir_) / path;
            std::error_code ec;
            if (fs::is_regular_file(full, ec)) tex = Texture::from_file(renderer_, full.string());
        }
        line_texture_cache_[path] = std::move(tex);
        auto loaded = line_texture_cache_.find(path);
        return loaded != line_texture_cache_.end() && loaded->second.valid() ? &loaded->second : nullptr;
    }
};

} // namespace

#if defined(PHIGROS_ANDROID) || defined(PHIGROS_IOS)
extern "C" int SDL_main(int, char**) {
#else
int main(int, char**) {
#endif
    phigros::core::Logger::get().set_level(phigros::core::LogLevel::Info);
    SdlMobileApp app;
    return app.run();
}
