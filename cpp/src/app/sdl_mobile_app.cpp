#include "phigros/app/sdl_compat.hpp"
#include "phigros/app/input_manager.hpp"
#include "phigros/chart/chart_loader.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/core/logger.hpp"
#include "phigros/engine/exact_autoplay.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/manual_judge.hpp"
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
#include "phigros/io/audio.hpp"
#include "phigros/io/respack.hpp"

#include <algorithm>
#include <array>
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

enum class Language {
    ZhCn,
    EnUs,
};

enum class StatusKind {
    None,
    NoCharts,
    FoundCharts,
    Seeked,
    LoadingChart,
    LoadError,
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

struct LibraryLayout {
    Rect settings;
    Rect rescan;
    Rect sort;
    Rect filter;
    Rect list;
    Rect preview;
    Rect prev;
    Rect next;
    Rect play;
    Rect details;
    Rect mode;
    bool wide = false;
    int rows = 0;
};

struct GradeCounts {
    int perfect = 0;
    int good = 0;
    int bad = 0;
    int miss = 0;
};

struct DetailLayout {
    Rect back;
    Rect play;
    Rect mode;
    Rect settings;
    Rect left;
    Rect right;
    bool wide = false;
};

struct SettingsLayout {
    Rect back;
    Rect tab_render;
    Rect tab_gameplay;
    Rect tab_fx;
    Rect content;
    double label_w = 0.0;
    double row_h = 62.0;
};

struct PlayLayout {
    Rect strip;
    Rect pause;
    Rect restart;
    Rect mode;
    Rect overlay;
    Rect seek;
    Rect speed_down;
    Rect speed_up;
    bool compact = false;
};

struct PauseLayout {
    Rect panel;
    Rect language;
    Rect resume;
    Rect restart;
    Rect library;
    Rect mode;
    Rect seek_back;
    Rect seek_forward;
};

struct ResultLayout {
    Rect panel;
    Rect language;
    Rect replay;
    Rect library;
    Rect detail;
};

class SdlMobileApp {
public:
    int run() {
        if (!phigros::app::sdl::sdl_init()) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        window_ = phigros::app::sdl::create_window(window_title(), width_, height_, false);
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
            input_.begin_frame();
            poll_events();
            update();
            draw();
            input_.flush_released();
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
    phigros::io::AudioSystem audio_;
    phigros::io::Respack respack_;
    RenderConfig cfg_;
    phigros::app::InputManager input_;
    phigros::engine::ManualJudge manual_judge_;

    std::vector<ChartEntry> library_;
    std::vector<int> visible_library_;
    int selected_ = -1;
    int library_page_ = 0;
    int sort_mode_ = 0;
    int filter_mode_ = 0;
    int settings_tab_ = 0;
    Language language_ = Language::ZhCn;
    StatusKind status_kind_ = StatusKind::None;
    size_t status_count_ = 0;
    std::string status_detail_;
    bool autoplay_ = true;
    bool show_particles_ = true;
    bool show_hitfx_ = true;
    bool show_gameplay_overlay_ = true;
    double last_frame_wall_ = 0.0;
    double fps_ = 60.0;

    std::unique_ptr<PlayingChart> playing_;
    phigros::engine::ScoreResult last_score_{};

    std::unordered_map<std::string, Texture> line_texture_cache_;
    std::string chart_dir_;
    bool chart_is_zip_ = false;
    std::string chart_zip_file_;
    bool audio_engine_ready_ = false;
    bool bgm_loaded_ = false;
    bool bgm_started_ = false;

    static double now_sec() {
        return static_cast<double>(SDL_GetPerformanceCounter()) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    }

    void destroy() {
        for (auto& item : line_texture_cache_) item.second.destroy();
        line_texture_cache_.clear();
        bg_.destroy();
        audio_.destroy();
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
        auto push_unique = [&](fs::path p) {
            if (p.empty()) return;
            p = p.lexically_normal();
            if (std::find(roots.begin(), roots.end(), p) == roots.end()) roots.push_back(std::move(p));
        };
        std::error_code ec;
        push_unique(fs::current_path(ec));
        if (!base_path().empty()) push_unique(base_path());
#if defined(PHIGROS_ANDROID)
        push_unique("/sdcard/phigros");
        push_unique("/sdcard/Phigros");
#endif
        return roots;
    }

    std::vector<fs::path> chart_search_roots() const {
        std::vector<fs::path> roots;
        auto push_unique_existing = [&](fs::path p) {
            if (p.empty()) return;
            p = p.lexically_normal();
            std::error_code ec;
            if (!fs::is_directory(p, ec)) return;
            if (std::find(roots.begin(), roots.end(), p) == roots.end()) roots.push_back(std::move(p));
        };
        for (const auto& root : search_roots()) {
            push_unique_existing(root / "charts");
            push_unique_existing(root / ".." / "charts");
        }
#if defined(PHIGROS_ANDROID)
        push_unique_existing("/sdcard/phigros");
        push_unique_existing("/sdcard/Phigros");
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
        status_kind_ = StatusKind::None;
        status_detail_.clear();
        status_count_ = 0;
        std::vector<std::string> seen;
        for (const auto& root : chart_search_roots()) {
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
        rebuild_visible_library();
        if (library_.empty()) {
            status_kind_ = StatusKind::NoCharts;
        } else {
            status_kind_ = StatusKind::FoundCharts;
            status_count_ = library_.size();
        }
    }

    bool entry_matches_filter(const ChartEntry& entry) const {
        if (filter_mode_ == 0) return true;
        if (filter_mode_ == 1) return entry.format == phigros::chart::ChartFormat::Official;
        if (filter_mode_ == 2) return entry.format == phigros::chart::ChartFormat::Rpe;
        if (filter_mode_ == 3) return entry.format == phigros::chart::ChartFormat::Pec ||
                                      entry.format == phigros::chart::ChartFormat::Phbc ||
                                      entry.format == phigros::chart::ChartFormat::Pbc ||
                                      entry.format == phigros::chart::ChartFormat::Unknown;
        return true;
    }

    void rebuild_visible_library() {
        visible_library_.clear();
        visible_library_.reserve(library_.size());
        for (int i = 0; i < static_cast<int>(library_.size()); ++i) {
            if (entry_matches_filter(library_[i])) visible_library_.push_back(i);
        }
        std::stable_sort(visible_library_.begin(), visible_library_.end(),
            [this](int lhs, int rhs) {
                const auto& a = library_[lhs];
                const auto& b = library_[rhs];
                if (sort_mode_ == 1) {
                    if (a.difficulty != b.difficulty) return a.difficulty < b.difficulty;
                } else if (sort_mode_ == 2) {
                    if (a.format != b.format) return static_cast<int>(a.format) < static_cast<int>(b.format);
                }
                const std::string an = entry_title(a);
                const std::string bn = entry_title(b);
                if (an != bn) return an < bn;
                return a.chart_path < b.chart_path;
            });

        bool selected_visible = false;
        for (int index : visible_library_) {
            if (index == selected_) {
                selected_visible = true;
                break;
            }
        }
        if (!selected_visible) selected_ = visible_library_.empty() ? -1 : visible_library_.front();
        library_page_ = std::clamp(library_page_, 0, max_library_page());
    }

    int max_library_page() const {
        const LibraryLayout l = library_layout();
        if (visible_library_.empty() || l.rows <= 0) return 0;
        return (static_cast<int>(visible_library_.size()) - 1) / l.rows;
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
            if (screen_ == Screen::Playing) {
                input_.process_event(e, width_, height_);
            }
            if (e.type == PHIGROS_SDL_EVENT_KEY_DOWN) {
                const auto sc = PHIGROS_KEY_SCANCODE(e);
                if (sc == SDL_SCANCODE_ESCAPE) {
                    back();
                } else if (sc == SDL_SCANCODE_SPACE && (screen_ == Screen::Playing || screen_ == Screen::Paused)) {
                    toggle_pause();
                } else if (sc == SDL_SCANCODE_L && screen_ != Screen::Playing) {
                    toggle_language();
                } else if (sc == SDL_SCANCODE_R && playing_) {
                    start_playing(selected_);
                } else if (sc == SDL_SCANCODE_LEFT && playing_) {
                    seek_relative(-10.0);
                } else if (sc == SDL_SCANCODE_RIGHT && playing_) {
                    seek_relative(10.0);
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
        input_.end_frame(dt);

        if (screen_ == Screen::Playing && playing_ && playing_->loaded) {
            update_playing(now);
        }
    }

    void update_playing(double now) {
        auto& p = *playing_;
        double dt = std::clamp(now - p.last_wall, 0.0, 0.1);
        p.last_wall = now;
        p.prev_t = p.t;
        if (bgm_loaded_) {
            if (!bgm_started_ && p.t >= 0.0) {
                audio_.play();
                bgm_started_ = true;
            }
            if (bgm_started_) {
                p.t = audio_.get_playback_time() + cfg_.audio_offset_ms / 1000.0;
            } else {
                p.t += dt * cfg_.chart_speed;
            }
        } else {
            p.t += dt * cfg_.chart_speed;
        }

        static thread_local std::vector<phigros::engine::SimHitEvent> hit_events;
        hit_events.clear();
        if (autoplay_) {
            phigros::engine::exact_autoplay_step(
                p.prev_t, p.t, p.chart.notes, p.states, p.chart.lines, p.judge,
                width_, height_, &hit_events);
            for (const auto& ev : hit_events) add_hit_effect(ev);
        } else {
            auto frame = phigros::render::build_frame(p.t, p.chart, p.states, p.judge, cfg_);
            auto input = filtered_gameplay_input();
            manual_judge_.process_frame(input, frame, p.chart.notes, p.states,
                                        p.judge, p.effects, p.t, width_, height_);
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
            stop_audio_playback();
            last_score_ = phigros::engine::compute_score(
                p.judge.acc_sum, p.judge.max_combo, p.chart.playable_count);
            screen_ = Screen::Result;
        }
    }

    phigros::engine::JudgeInputFrame filtered_gameplay_input() const {
        auto raw = input_.to_judge_input();
        phigros::engine::JudgeInputFrame filtered;
        const double gameplay_bottom = height_ - control_strip_h();
        for (int i = 0; i < raw.count; ++i) {
            const auto& action = raw.actions[i];
            if (action.has_position && action.y > gameplay_bottom &&
                manual_judge_.holding_map.find(action.id) == manual_judge_.holding_map.end()) {
                continue;
            }
            filtered.add(action);
        }
        return filtered;
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

    void reset_play_state(double target_t) {
        if (!playing_ || !playing_->loaded) return;
        auto& p = *playing_;
        p.states.clear();
        p.states.resize(p.chart.notes.size());
        for (size_t i = 0; i < p.states.size(); ++i) p.states[i].note = &p.chart.notes[i];
        p.judge = Judge{};
        p.effects = phigros::engine::EffectManager{};
        p.effects.particle_count = cfg_.particle_count;
        reset_manual_judge();
        p.t = std::clamp(target_t, p.chart.offset - 1.0, p.chart_end);
        p.prev_t = p.t;
        p.last_wall = now_sec();
        p.finished = false;
        sync_audio_to_chart_time(p.t, screen_ == Screen::Playing);
        status_kind_ = StatusKind::Seeked;
        status_detail_.clear();
    }

    void seek_relative(double delta) {
        if (!playing_ || !playing_->loaded) return;
        reset_play_state(playing_->t + delta);
    }

    void seek_ratio(double ratio) {
        if (!playing_ || !playing_->loaded) return;
        const double start = playing_->chart.offset - 1.0;
        const double end = std::max(start + 1.0, playing_->chart_end);
        reset_play_state(start + std::clamp(ratio, 0.0, 1.0) * (end - start));
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

    const char* tr(const char* zh, const char* en) const {
        return language_ == Language::ZhCn ? zh : en;
    }

    const char* window_title() const {
        return tr("Phigros 渲染器", "Phigros Renderer");
    }

    const char* language_button_label() const {
        return language_ == Language::ZhCn ? "EN" : "中文";
    }

    void toggle_language() {
        language_ = language_ == Language::ZhCn ? Language::EnUs : Language::ZhCn;
        if (window_) {
            SDL_SetWindowTitle(window_, window_title());
        }
    }

    std::string status_text() const {
        char buf[256];
        switch (status_kind_) {
            case StatusKind::None:
                return {};
            case StatusKind::NoCharts:
                return tr("未找到谱面，请将文件夹或 zip 放入 charts/。",
                          "No charts found. Put folders or zip packs in charts/.");
            case StatusKind::FoundCharts:
                if (language_ == Language::ZhCn) {
                    std::snprintf(buf, sizeof(buf), "找到 %zu 个谱面。", status_count_);
                } else {
                    std::snprintf(buf, sizeof(buf), "Found %zu chart%s.",
                                  status_count_, status_count_ == 1 ? "" : "s");
                }
                return buf;
            case StatusKind::Seeked:
                return tr("已跳转。", "Seeked.");
            case StatusKind::LoadingChart:
                return tr("正在加载谱面...", "Loading chart...");
            case StatusKind::LoadError:
                return std::string(tr("谱面加载失败：", "Chart load failed: ")) + status_detail_;
        }
        return {};
    }

    void draw_text(const std::string& text, double x, double y,
                   uint8_t r = 235, uint8_t g = 240, uint8_t b = 245, uint8_t a = 235,
                   bool large = false) const {
        if (!hud_.has_font) return;
        hud_.draw_text(batch_, large ? hud_.font_large : hud_.font_small, text, x, y, r, g, b, a);
    }

    std::string fit_text(const std::string& text, double max_w, bool large = false) const {
        if (!hud_.has_font || max_w <= 0.0) return text;
        const auto& font = large ? hud_.font_large : hud_.font_small;
        if (hud_.text_width(font, text) <= max_w) return text;
        static const std::string tail = "...";
        std::string out = text;
        while (!out.empty() && hud_.text_width(font, out + tail) > max_w) {
            pop_utf8_tail(out);
        }
        return out.empty() ? tail : out + tail;
    }

    static void pop_utf8_tail(std::string& text) {
        if (text.empty()) return;
        size_t pos = text.size() - 1;
        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xc0) == 0x80) {
            --pos;
        }
        text.erase(pos);
    }

    const char* mode_label(bool short_label = false) const {
        if (short_label) {
            return autoplay_
                ? tr("自动", "Auto")
                : tr("手动", "Manual");
        }
        return autoplay_
            ? tr("自动播放", "Autoplay")
            : tr("手动模式", "Manual");
    }

    std::string field_line(const char* zh, const char* en, const std::string& value) const {
        return std::string(tr(zh, en)) + "  " + value;
    }

    std::string chart_count_line() const {
        char buf[128];
        if (language_ == Language::ZhCn) {
            std::snprintf(buf, sizeof(buf), "%zu / %zu 个条目",
                          visible_library_.size(), library_.size());
        } else {
            std::snprintf(buf, sizeof(buf), "%zu / %zu entries",
                          visible_library_.size(), library_.size());
        }
        return buf;
    }

    std::string page_line(int page, int total) const {
        char buf[64];
        if (language_ == Language::ZhCn) {
            std::snprintf(buf, sizeof(buf), "第 %d / %d 页", page, total);
        } else {
            std::snprintf(buf, sizeof(buf), "Page %d / %d", page, total);
        }
        return buf;
    }

    void draw_text_fit(const std::string& text, double x, double y, double max_w,
                       uint8_t r = 235, uint8_t g = 240, uint8_t b = 245, uint8_t a = 235,
                       bool large = false) const {
        draw_text(fit_text(text, max_w, large), x, y, r, g, b, a, large);
    }

    void draw_panel(const Rect& rect, uint8_t r = 18, uint8_t g = 22, uint8_t b = 29,
                    uint8_t a = 238) const {
        batch_.draw_rect(rect.x, rect.y, rect.w, rect.h, r, g, b, a);
        batch_.draw_line(rect.x, rect.y, rect.x + rect.w, rect.y, 1.0, 72, 84, 100, 190);
        batch_.draw_line(rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0, 6, 7, 10, 220);
    }

    void draw_button(const Rect& rect, const std::string& label, bool enabled = true) const {
        const uint8_t base = enabled ? 42 : 28;
        batch_.draw_rect(rect.x, rect.y, rect.w, rect.h, base, base + 5, base + 12, 235);
        batch_.draw_line(rect.x, rect.y, rect.x + rect.w, rect.y, 1.0, 95, 110, 130, 210);
        batch_.draw_line(rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0, 22, 24, 28, 210);
        if (hud_.has_font) {
            const auto& font = hud_.font_small;
            const std::string fitted = fit_text(label, rect.w - 14.0);
            double tw = hud_.text_width(font, fitted);
            double th = hud_.text_line_height(font);
            draw_text(fitted, rect.x + (rect.w - tw) * 0.5, rect.y + (rect.h - th) * 0.5,
                      enabled ? 238 : 120, enabled ? 242 : 128, enabled ? 248 : 136, 235);
        }
    }

    void draw_choice_button(const Rect& rect, const std::string& label, bool active,
                            bool enabled = true) const {
        const uint8_t br = active ? 68 : (enabled ? 42 : 28);
        const uint8_t bg = active ? 118 : (enabled ? 47 : 33);
        const uint8_t bb = active ? 138 : (enabled ? 54 : 40);
        batch_.draw_rect(rect.x, rect.y, rect.w, rect.h, br, bg, bb, 238);
        batch_.draw_rect(rect.x, rect.y, 4.0, rect.h,
                         active ? 122 : 62, active ? 216 : 150, active ? 246 : 185, 235);
        batch_.draw_line(rect.x, rect.y, rect.x + rect.w, rect.y, 1.0, 95, 110, 130, 210);
        batch_.draw_line(rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h,
                         1.0, 22, 24, 28, 210);
        if (hud_.has_font) {
            const std::string fitted = fit_text(label, rect.w - 14.0);
            const double tw = hud_.text_width(hud_.font_small, fitted);
            const double th = hud_.text_line_height(hud_.font_small);
            draw_text(fitted, rect.x + (rect.w - tw) * 0.5, rect.y + (rect.h - th) * 0.5,
                      enabled ? 238 : 120, enabled ? 242 : 128, enabled ? 248 : 136, 235);
        }
    }

    Rect top_language_rect() const {
        const double w = width_ < 520 ? 66.0 : 78.0;
        return {static_cast<double>(width_) - w - 20.0, 12.0, w, 40.0};
    }

    bool has_top_language_button() const {
        return screen_ == Screen::Library || screen_ == Screen::Detail ||
               screen_ == Screen::Settings ||
               (screen_ == Screen::Playing && (!playing_ || !playing_->loaded));
    }

    void draw_top_bar(const std::string& title) {
        const Rect lang = top_language_rect();
        batch_.draw_rect(0, 0, width_, 64, 15, 17, 22, 245);
        draw_text_fit(title, 24, 18, std::max(80.0, lang.x - 48.0),
                      245, 248, 252, 245, true);
        draw_button(lang, language_button_label());
        char fps_buf[48];
        std::snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS", fps_);
        if (hud_.has_font) {
            const double tw = hud_.text_width(hud_.font_small, fps_buf);
            const double fps_x = lang.x - tw - 14.0;
            if (fps_x > 24.0) {
                draw_text(fps_buf, fps_x, 22, 150, 170, 190, 220);
            }
        }
    }

    double library_row_h() const {
        return width_ < 640 ? 54.0 : 60.0;
    }

    double control_strip_h() const {
        return std::clamp(height_ * 0.16, 92.0, 126.0);
    }

    LibraryLayout library_layout() const {
        LibraryLayout l;
        const double pad = width_ < 700 ? 14.0 : 24.0;
        const double gap = width_ < 700 ? 8.0 : 12.0;
        const double button_h = 42.0;
        const bool narrow = width_ < 760;
        l.wide = width_ >= 1040 && height_ >= 600;

        if (narrow) {
            const double bw = (width_ - pad * 2.0 - gap) * 0.5;
            l.sort = {pad, 78.0, bw, button_h};
            l.filter = {pad + bw + gap, 78.0, bw, button_h};
            l.settings = {pad, 128.0, bw, button_h};
            l.rescan = {pad + bw + gap, 128.0, bw, button_h};
        } else {
            l.sort = {pad, 82.0, 122.0, button_h};
            l.filter = {pad + 134.0, 82.0, 130.0, button_h};
            l.settings = {width_ - pad - 226.0, 82.0, 104.0, button_h};
            l.rescan = {width_ - pad - 110.0, 82.0, 110.0, button_h};
        }

        const double list_top = narrow ? 188.0 : 144.0;
        const double footer_h = narrow ? 112.0 : 72.0;
        const double list_h = std::max(80.0, height_ - list_top - footer_h);
        if (l.wide) {
            const double list_w = std::min(620.0, width_ * 0.50);
            l.list = {pad, list_top, list_w, list_h};
            l.preview = {l.list.x + l.list.w + 22.0, list_top,
                         width_ - pad - (l.list.x + l.list.w + 22.0), list_h};
        } else {
            l.list = {pad, list_top, width_ - pad * 2.0, list_h};
            l.preview = {pad, list_top + list_h + 8.0, width_ - pad * 2.0, 0.0};
        }
        l.rows = std::max(1, static_cast<int>(l.list.h / library_row_h()));

        if (narrow) {
            const double nav_y = height_ - 106.0;
            l.prev = {pad, nav_y, 84.0, button_h};
            l.next = {pad + 94.0, nav_y, 84.0, button_h};
            const double action_y = height_ - 56.0;
            const double action_w = std::min(98.0, (width_ - pad * 2.0 - gap * 2.0) / 3.0);
            l.details = {pad, action_y, action_w, button_h};
            l.mode = {pad + action_w + gap, action_y, action_w, button_h};
            l.play = {pad + (action_w + gap) * 2.0, action_y, action_w, button_h};
        } else if (l.wide) {
            const double y = l.preview.y + l.preview.h - 54.0;
            l.mode = {l.preview.x + 18.0, y, 122.0, button_h};
            l.details = {l.preview.x + l.preview.w - 250.0, y, 112.0, button_h};
            l.play = {l.preview.x + l.preview.w - 126.0, y, 108.0, button_h};
            l.prev = {pad, height_ - 58.0, 96.0, button_h};
            l.next = {pad + 108.0, height_ - 58.0, 96.0, button_h};
        } else {
            l.prev = {pad, height_ - 58.0, 96.0, button_h};
            l.next = {pad + 108.0, height_ - 58.0, 96.0, button_h};
            l.play = {width_ - pad - 104.0, height_ - 58.0, 104.0, button_h};
            l.details = {l.play.x - 116.0, height_ - 58.0, 104.0, button_h};
            l.mode = {l.details.x - 126.0, height_ - 58.0, 114.0, button_h};
        }
        return l;
    }

    DetailLayout detail_layout() const {
        DetailLayout l;
        const double pad = width_ < 700 ? 16.0 : 24.0;
        l.wide = width_ >= 980;
        l.back = {pad, 84.0, 96.0, 44.0};
        const bool compact_actions = width_ < 560;
        if (compact_actions) {
            l.settings = {width_ - pad - 112.0, 84.0, 112.0, 44.0};
            l.mode = {pad, 136.0, 120.0, 44.0};
            l.play = {width_ - pad - 112.0, 136.0, 112.0, 44.0};
        } else {
            l.settings = {width_ - pad - 112.0, 84.0, 112.0, 44.0};
            l.play = {l.settings.x - 126.0, 84.0, 112.0, 44.0};
            l.mode = {l.play.x - 132.0, 84.0, 120.0, 44.0};
        }
        const double top = compact_actions ? 202.0 : 150.0;
        const double h = std::max(220.0, height_ - top - 26.0);
        if (l.wide) {
            l.left = {pad, top, std::min(590.0, width_ * 0.47), h};
            l.right = {l.left.x + l.left.w + 22.0, top,
                       width_ - pad - (l.left.x + l.left.w + 22.0), h};
        } else {
            l.left = {pad, top, width_ - pad * 2.0, h};
            l.right = {pad, top + h, width_ - pad * 2.0, 0.0};
        }
        return l;
    }

    SettingsLayout settings_layout() const {
        SettingsLayout l;
        const double pad = width_ < 700 ? 16.0 : 24.0;
        const double gap = width_ < 560 ? 8.0 : 12.0;
        l.back = {pad, 84.0, 96.0, 44.0};
        const double tab_y = 142.0;
        const double tab_w = (width_ - pad * 2.0 - gap * 2.0) / 3.0;
        l.tab_render = {pad, tab_y, tab_w, 42.0};
        l.tab_gameplay = {pad + tab_w + gap, tab_y, tab_w, 42.0};
        l.tab_fx = {pad + (tab_w + gap) * 2.0, tab_y, tab_w, 42.0};
        l.content = {pad, 204.0, width_ - pad * 2.0, std::max(120.0, height_ - 226.0)};
        l.label_w = std::clamp(width_ * 0.25, 138.0, 240.0);
        l.row_h = width_ < 560 ? 58.0 : 64.0;
        return l;
    }

    PlayLayout play_layout() const {
        PlayLayout l;
        l.compact = width_ < 760;
        l.strip = {0.0, height_ - control_strip_h(), static_cast<double>(width_), control_strip_h()};
        const double pad = l.compact ? 12.0 : 18.0;
        const double gap = l.compact ? 6.0 : 10.0;
        if (l.compact) {
            l.seek = {pad, l.strip.y + 12.0, width_ - pad * 2.0, 28.0};
            const double row_y = l.strip.y + l.strip.h - 50.0;
            const double speed_w = 36.0;
            const double inner = width_ - pad * 2.0;
            const double main_w = std::max(80.0, inner - speed_w * 2.0 - gap * 5.0);
            const double pause_w = std::floor(main_w * 0.22);
            const double restart_w = std::floor(main_w * 0.24);
            const double mode_w = std::floor(main_w * 0.29);
            const double overlay_w = main_w - pause_w - restart_w - mode_w;
            double x = pad;
            l.pause = {x, row_y, pause_w, 42.0}; x += pause_w + gap;
            l.restart = {x, row_y, restart_w, 42.0}; x += restart_w + gap;
            l.mode = {x, row_y, mode_w, 42.0}; x += mode_w + gap;
            l.overlay = {x, row_y, overlay_w, 42.0}; x += overlay_w + gap;
            l.speed_down = {x, row_y, speed_w, 42.0}; x += speed_w + gap;
            l.speed_up = {x, row_y, speed_w, 42.0};
        } else {
            const double row_y = l.strip.y + l.strip.h - 56.0;
            l.pause = {pad, row_y, 86.0, 42.0};
            l.restart = {l.pause.x + l.pause.w + gap, row_y, 92.0, 42.0};
            l.mode = {l.restart.x + l.restart.w + gap, row_y, 118.0, 42.0};
            l.overlay = {l.mode.x + l.mode.w + gap, row_y, 108.0, 42.0};
            l.speed_down = {width_ - pad - 94.0, row_y, 42.0, 42.0};
            l.speed_up = {width_ - pad - 42.0, row_y, 42.0, 42.0};
            const double sx = l.overlay.x + l.overlay.w + 18.0;
            l.seek = {sx, l.strip.y + 23.0, std::max(120.0, l.speed_down.x - sx - 18.0), 30.0};
        }
        return l;
    }

    PauseLayout pause_layout() const {
        PauseLayout l;
        const double panel_w = std::min(width_ - 40.0, width_ < 720 ? 420.0 : 520.0);
        const double panel_h = width_ < 720 ? 344.0 : 374.0;
        l.panel = {(width_ - panel_w) * 0.5, (height_ - panel_h) * 0.5, panel_w, panel_h};
        const double x = l.panel.x + 24.0;
        const double y = l.panel.y + panel_h - 70.0;
        const double bw = (panel_w - 58.0) / 3.0;
        l.language = {l.panel.x + l.panel.w - 92.0, l.panel.y + 20.0, 70.0, 38.0};
        l.resume = {x, y, bw, 44.0};
        l.restart = {x + bw + 5.0, y, bw, 44.0};
        l.library = {x + (bw + 5.0) * 2.0, y, bw, 44.0};
        l.mode = {x, l.panel.y + 154.0, 126.0, 42.0};
        l.seek_back = {x + 138.0, l.panel.y + 154.0, 118.0, 42.0};
        l.seek_forward = {x + 268.0, l.panel.y + 154.0, std::max(96.0, panel_w - 316.0), 42.0};
        return l;
    }

    ResultLayout result_layout() const {
        ResultLayout l;
        const double panel_w = std::min(width_ - 40.0, width_ < 720 ? 440.0 : 560.0);
        const double panel_h = width_ < 720 ? 384.0 : 414.0;
        l.panel = {(width_ - panel_w) * 0.5, (height_ - panel_h) * 0.5, panel_w, panel_h};
        const double x = l.panel.x + 26.0;
        const double y = l.panel.y + panel_h - 66.0;
        const double bw = (panel_w - 62.0) / 3.0;
        l.language = {l.panel.x + l.panel.w - 92.0, l.panel.y + 20.0, 70.0, 38.0};
        l.replay = {x, y, bw, 44.0};
        l.detail = {x + bw + 5.0, y, bw, 44.0};
        l.library = {x + (bw + 5.0) * 2.0, y, bw, 44.0};
        return l;
    }

    Rect library_row_rect(const LibraryLayout& l, int row) const {
        return {l.list.x, l.list.y + row * library_row_h(), l.list.w, library_row_h() - 10.0};
    }

    std::string entry_title(const ChartEntry& entry) const {
        if (!entry.metadata.name.empty()) return entry.metadata.name;
        if (!entry.name.empty()) return entry.name;
        return fs::path(entry.chart_path).stem().string();
    }

    std::string entry_level(const ChartEntry& entry) const {
        if (!entry.metadata.level.empty()) return entry.metadata.level;
        if (!entry.difficulty.empty()) return entry.difficulty;
        if (entry.metadata.difficulty > 0.0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", entry.metadata.difficulty);
            return buf;
        }
        return "-";
    }

    std::string entry_subtitle(const ChartEntry& entry) const {
        std::string line = entry_level(entry);
        const std::string format = phigros::chart::chart_format_name(entry.format);
        if (!format.empty()) line += "  " + format;
        if (!entry.source_type.empty()) line += "  " + entry.source_type;
        return line;
    }

    const ChartEntry* selected_entry() const {
        if (selected_ < 0 || selected_ >= static_cast<int>(library_.size())) return nullptr;
        return &library_[selected_];
    }

    double chart_progress_ratio() const {
        if (!playing_ || !playing_->loaded) return 0.0;
        const double start = playing_->chart.offset - 1.0;
        const double end = std::max(start + 1.0, playing_->chart_end);
        return std::clamp((playing_->t - start) / (end - start), 0.0, 1.0);
    }

    static std::string format_time(double t) {
        if (!std::isfinite(t)) t = 0.0;
        const int total = std::max(0, static_cast<int>(std::round(t)));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
        return buf;
    }

    GradeCounts count_grades() const {
        GradeCounts counts;
        if (!playing_) return counts;
        for (const auto& state : playing_->states) {
            if (state.judge_grade == "PERFECT") ++counts.perfect;
            else if (state.judge_grade == "GOOD") ++counts.good;
            else if (state.judge_grade == "BAD") ++counts.bad;
            else if (state.judge_grade == "MISS" || state.miss) ++counts.miss;
        }
        return counts;
    }

    void draw_library() {
        draw_top_bar(window_title());
        const LibraryLayout l = library_layout();
        const std::array<const char*, 3> sort_labels{
            tr("按曲名", "By Title"),
            tr("按难度", "By Level"),
            tr("按格式", "By Format"),
        };
        const std::array<const char*, 4> filter_labels{
            tr("全部", "All"),
            tr("官方", "Official"),
            "RPE",
            tr("其他", "Other"),
        };
        draw_button(l.sort, sort_labels[std::clamp(sort_mode_, 0, 2)]);
        draw_button(l.filter, filter_labels[std::clamp(filter_mode_, 0, 3)]);
        draw_button(l.settings, tr("设置", "Settings"));
        draw_button(l.rescan, tr("重扫", "Rescan"));

        const std::string count = chart_count_line();
        const std::string status = status_text();
        draw_text_fit(status.empty() ? count : status + "  " + count,
                      l.list.x, l.list.y - 28.0, l.list.w, 156, 176, 196, 225);

        const int max_page = max_library_page();
        library_page_ = std::clamp(library_page_, 0, max_page);
        const int start = library_page_ * l.rows;
        const int end = std::min(static_cast<int>(visible_library_.size()), start + l.rows);

        for (int i = start; i < end; ++i) {
            const int row_i = i - start;
            const Rect row = library_row_rect(l, row_i);
            const int entry_index = visible_library_[i];
            const bool chosen = entry_index == selected_;
            batch_.draw_rect(row.x, row.y, row.w, row.h,
                             chosen ? 58 : 28, chosen ? 70 : 34, chosen ? 86 : 44, 235);
            batch_.draw_rect(row.x, row.y, 4.0, row.h,
                             chosen ? 115 : 62, chosen ? 210 : 150, chosen ? 255 : 185, 235);
            const auto& entry = library_[entry_index];
            draw_text_fit(entry_title(entry), row.x + 16.0, row.y + 8.0, row.w - 32.0,
                          236, 241, 246, 238);
            draw_text_fit(entry_subtitle(entry), row.x + 16.0, row.y + 31.0, row.w - 32.0,
                          145, 166, 188, 220);
        }

        if (l.wide) draw_library_preview(l);
        draw_button(l.prev, tr("上一页", "Prev"), library_page_ > 0);
        draw_button(l.next, tr("下一页", "Next"), library_page_ < max_page);
        draw_button(l.mode, mode_label(), selected_ >= 0);
        draw_button(l.details, tr("详情", "Details"), selected_ >= 0);
        draw_button(l.play, tr("开始", "Play"), selected_ >= 0);
        const std::string page = page_line(library_page_ + 1, max_page + 1);
        const double page_x = l.next.x + l.next.w + 12.0;
        draw_text_fit(page, page_x, l.prev.y + 11.0,
                      std::max(50.0, width_ - page_x - 24.0), 160, 178, 196, 220);
    }

    void draw_library_preview(const LibraryLayout& l) {
        draw_panel(l.preview, 17, 21, 28, 232);
        const ChartEntry* entry = selected_entry();
        if (!entry) {
            draw_text(tr("未选择谱面", "No chart selected"),
                      l.preview.x + 22.0, l.preview.y + 22.0,
                      180, 194, 210, 225, true);
            return;
        }
        const double x = l.preview.x + 22.0;
        double y = l.preview.y + 22.0;
        draw_text_fit(entry_title(*entry), x, y, l.preview.w - 44.0,
                      248, 250, 255, 245, true);
        y += 54.0;
        draw_text_fit(field_line("难度", "Level", entry_level(*entry)),
                      x, y, l.preview.w - 44.0); y += 32.0;
        draw_text_fit(field_line("格式", "Format", phigros::chart::chart_format_name(entry->format)),
                      x, y, l.preview.w - 44.0); y += 32.0;
        draw_text_fit(field_line("来源", "Source",
                                 entry->source_type.empty() ? std::string("-") : entry->source_type),
                      x, y, l.preview.w - 44.0); y += 44.0;
        draw_text_fit(tr("谱面文件", "Chart File"),
                      x, y, l.preview.w - 44.0, 116, 210, 238, 230); y += 26.0;
        draw_text_fit(compact_path(entry->chart_path), x, y, l.preview.w - 44.0,
                      160, 178, 196, 220); y += 34.0;
        draw_text_fit(field_line("音乐", "Music",
                                 entry->assets.music_path.empty() ? std::string("-") : compact_path(entry->assets.music_path)),
                      x, y, l.preview.w - 44.0, 160, 178, 196, 220); y += 30.0;
        draw_text_fit(field_line("曲绘", "Illustration",
                                 entry->assets.illustration_path.empty() ? std::string("-") : compact_path(entry->assets.illustration_path)),
                      x, y, l.preview.w - 44.0, 160, 178, 196, 220);
    }

    void draw_detail() {
        draw_top_bar(tr("谱面详情", "Chart Details"));
        const DetailLayout l = detail_layout();
        if (selected_ < 0 || selected_ >= static_cast<int>(library_.size())) {
            draw_text(tr("未选择谱面。", "No chart selected."), 32, 100);
            return;
        }
        const auto& e = library_[selected_];
        draw_button(l.back, tr("返回", "Back"));
        draw_button(l.mode, mode_label());
        draw_button(l.play, tr("开始", "Play"));
        draw_button(l.settings, tr("设置", "Settings"));

        draw_panel(l.left, 17, 21, 28, 232);
        const double x = l.left.x + 22.0;
        double y = l.left.y + 22.0;
        draw_text_fit(entry_title(e), x, y, l.left.w - 44.0, 248, 250, 255, 245, true);
        y += 56.0;
        draw_text_fit(field_line("难度", "Level", entry_level(e)), x, y, l.left.w - 44.0); y += 32.0;
        draw_text_fit(field_line("格式", "Format", phigros::chart::chart_format_name(e.format)),
                      x, y, l.left.w - 44.0); y += 32.0;
        draw_text_fit(field_line("来源", "Source", e.source_type.empty() ? std::string("-") : e.source_type),
                      x, y, l.left.w - 44.0); y += 32.0;
        if (!e.metadata.composer.empty()) {
            draw_text_fit(field_line("曲师", "Composer", e.metadata.composer),
                          x, y, l.left.w - 44.0); y += 32.0;
        }
        if (!e.metadata.charter.empty()) {
            draw_text_fit(field_line("谱师", "Charter", e.metadata.charter),
                          x, y, l.left.w - 44.0); y += 32.0;
        }
        if (!e.metadata.illustrator.empty()) {
            draw_text_fit(field_line("画师", "Illustrator", e.metadata.illustrator),
                          x, y, l.left.w - 44.0); y += 32.0;
        }

        const Rect info = l.wide ? l.right : Rect{l.left.x, y + 18.0, l.left.w,
                                                  std::max(80.0, l.left.y + l.left.h - y - 18.0)};
        draw_panel(info, 14, 18, 25, 220);
        double iy = info.y + 20.0;
        const double ix = info.x + 20.0;
        draw_text(tr("资源", "Resources"), ix, iy, 116, 210, 238, 235); iy += 34.0;
        draw_text_fit(field_line("谱面", "Chart", compact_path(e.chart_path)), ix, iy, info.w - 40.0,
                      185, 198, 214, 225); iy += 32.0;
        draw_text_fit(field_line("音乐", "Music",
                                 e.assets.music_path.empty() ? std::string("-") : compact_path(e.assets.music_path)),
                      ix, iy, info.w - 40.0, 185, 198, 214, 225); iy += 32.0;
        draw_text_fit(field_line("曲绘", "Illustration",
                                 e.assets.illustration_path.empty() ? std::string("-") : compact_path(e.assets.illustration_path)),
                      ix, iy, info.w - 40.0, 185, 198, 214, 225); iy += 42.0;
        char meta[128];
        if (language_ == Language::ZhCn) {
            std::snprintf(meta, sizeof(meta), "预览 %.1fs  背景暗化 %d  模糊 %d",
                          e.metadata.preview_start, cfg_.bg_dim, cfg_.bg_blur);
        } else {
            std::snprintf(meta, sizeof(meta), "Preview %.1fs  Dim %d  Blur %d",
                          e.metadata.preview_start, cfg_.bg_dim, cfg_.bg_blur);
        }
        draw_text_fit(meta, ix, iy, info.w - 40.0, 150, 170, 190, 220);
    }

    void draw_settings() {
        draw_top_bar(tr("设置", "Settings"));
        const SettingsLayout l = settings_layout();
        draw_button(l.back, tr("返回", "Back"));
        draw_choice_button(l.tab_render, tr("渲染", "Render"), settings_tab_ == 0);
        draw_choice_button(l.tab_gameplay, tr("游玩", "Gameplay"), settings_tab_ == 1);
        draw_choice_button(l.tab_fx, tr("特效", "FX"), settings_tab_ == 2);
        draw_panel(l.content, 15, 19, 26, 230);

        double y = l.content.y + 20.0;
        if (settings_tab_ == 0) {
            draw_setting_button(l, tr("语言", "Language"),
                                language_ == Language::ZhCn ? "中文" : "English", y); y += l.row_h;
            draw_slider(l, tr("背景暗化", "Background Dim"),
                        static_cast<double>(cfg_.bg_dim), 0.0, 220.0, y); y += l.row_h;
            draw_slider(l, tr("音符透明度", "Note Alpha"), cfg_.note_alpha, 0.1, 1.0, y); y += l.row_h;
            draw_slider(l, tr("提前显示", "Approach"), cfg_.approach, 0.5, 8.0, y); y += l.row_h;
            draw_slider(l, tr("画面扩展", "Canvas Expand"), cfg_.expand_factor, 0.8, 1.4, y); y += l.row_h;
            draw_setting_toggle(l, tr("音符描边", "Note Outline"), cfg_.note_outline, y);
        } else if (settings_tab_ == 1) {
            draw_setting_toggle(l, tr("自动播放", "Autoplay"), autoplay_, y); y += l.row_h;
            draw_setting_toggle(l, tr("界面显示", "HUD"), show_gameplay_overlay_, y); y += l.row_h;
            draw_slider(l, tr("谱面速度", "Chart Speed"), cfg_.chart_speed, 0.5, 3.0, y); y += l.row_h;
            draw_slider(l, tr("长条特效间隔", "Hold FX Gap"),
                        static_cast<double>(cfg_.hold_fx_interval_ms), 80.0, 400.0, y); y += l.row_h;
            draw_slider(l, tr("长条尾判容差", "Hold Tail Tolerance"), cfg_.hold_tail_tol, 0.2, 1.2, y);
        } else {
            draw_setting_toggle(l, tr("打击特效", "Hit FX"), show_hitfx_, y); y += l.row_h;
            draw_setting_toggle(l, tr("粒子", "Particles"), show_particles_, y); y += l.row_h;
            draw_slider(l, tr("特效强度", "FX Intensity"), cfg_.hitfx_intensity, 0.0, 2.0, y); y += l.row_h;
            draw_slider(l, tr("粒子数量", "Particle Count"),
                        static_cast<double>(cfg_.particle_count), 0.0, 32.0, y); y += l.row_h;
            draw_slider(l, tr("横向缩放", "Scale X"), cfg_.note_scale_x, 1.0, 4.0, y); y += l.row_h;
            draw_slider(l, tr("纵向缩放", "Scale Y"), cfg_.note_scale_y, 0.5, 2.0, y);
        }
    }

    Rect settings_control_rect(const SettingsLayout& l, double y, double w = 116.0) const {
        const double x = l.content.x + l.label_w;
        return {x, y, std::min(w, l.content.x + l.content.w - x - 18.0), 44.0};
    }

    Rect settings_slider_rect(const SettingsLayout& l, double y) const {
        const double x = l.content.x + l.label_w;
        return {x, y + 7.0, std::max(80.0, l.content.x + l.content.w - x - 20.0), 30.0};
    }

    void draw_setting_toggle(const SettingsLayout& l, const std::string& label,
                             bool value, double y) {
        draw_setting_button(l, label, value ? tr("开", "On") : tr("关", "Off"), y);
    }

    void draw_setting_button(const SettingsLayout& l, const std::string& label,
                             const std::string& value, double y) {
        draw_text_fit(label, l.content.x + 18.0, y + 11.0, l.label_w - 28.0);
        draw_button(settings_control_rect(l, y), value);
    }

    void draw_slider(const SettingsLayout& l, const std::string& label,
                     double value, double min_v, double max_v, double y) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  %.2f", label.c_str(), value);
        draw_text_fit(buf, l.content.x + 18.0, y + 3.0, l.label_w - 28.0);
        const Rect s = settings_slider_rect(l, y);
        batch_.draw_rect(s.x, s.y + 12.0, s.w, 6.0, 45, 50, 58, 255);
        double p = std::clamp((value - min_v) / std::max(0.0001, max_v - min_v), 0.0, 1.0);
        batch_.draw_rect(s.x, s.y + 12.0, s.w * p, 6.0, 105, 205, 245, 255);
        batch_.draw_rect(s.x + s.w * p - 7.0, s.y, 14.0, 30.0, 235, 240, 245, 255);
    }

    void draw_playing(bool frozen) {
        if (!playing_ || !playing_->loaded) {
            draw_top_bar(tr("加载中", "Loading"));
            const std::string status = status_text();
            draw_text(status.empty() ? tr("未加载谱面。", "No chart loaded.") : status, 32, 110);
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

        if (show_gameplay_overlay_) hud_.draw(batch_, frame.hud, fps_);
        draw_play_controls(frozen);
    }

    void draw_play_controls(bool frozen) {
        if (!playing_) return;
        const PlayLayout l = play_layout();
        batch_.draw_rect(l.strip.x, l.strip.y, l.strip.w, l.strip.h, 8, 10, 14,
                         show_gameplay_overlay_ ? 206 : 156);
        batch_.draw_line(0, l.strip.y, width_, l.strip.y, 1.0, 78, 92, 108, 180);
        draw_button(l.pause, frozen ? tr("已暂停", "Paused") : tr("暂停", "Pause"));
        draw_button(l.restart, tr("重开", "Restart"));
        draw_button(l.mode, mode_label(true));
        draw_button(l.overlay, show_gameplay_overlay_ ? tr("界面开", "HUD On") : tr("界面关", "HUD Off"));
        draw_button(l.speed_down, "-");
        draw_button(l.speed_up, "+");

        const double p = chart_progress_ratio();
        batch_.draw_rect(l.seek.x, l.seek.y + 11.0, l.seek.w, 8.0, 45, 50, 58, 245);
        batch_.draw_rect(l.seek.x, l.seek.y + 11.0, l.seek.w * p, 8.0, 105, 205, 245, 250);
        batch_.draw_rect(l.seek.x + l.seek.w * p - 5.0, l.seek.y + 3.0, 10.0, 24.0,
                         235, 240, 245, 245);
        const double current = std::max(0.0, playing_->t - (playing_->chart.offset - 1.0));
        const double total = std::max(1.0, playing_->chart_end - (playing_->chart.offset - 1.0));
        const std::string label = format_time(current) + " / " + format_time(total);
        draw_text_fit(label, l.seek.x, l.seek.y - 18.0, l.seek.w * 0.55,
                      150, 170, 190, 220);
        char speed[48];
        std::snprintf(speed, sizeof(speed), "%.2fx", cfg_.chart_speed);
        if (hud_.has_font) {
            const double tw = hud_.text_width(hud_.font_small, speed);
            draw_text(speed, l.seek.x + l.seek.w - tw, l.seek.y - 18.0,
                      150, 170, 190, 220);
        }
    }

    void draw_pause_menu() {
        batch_.draw_rect(0, 0, width_, height_, 0, 0, 0, 130);
        const PauseLayout l = pause_layout();
        draw_panel(l.panel, 18, 22, 29, 248);
        const double x = l.panel.x + 24.0;
        double y = l.panel.y + 24.0;
        draw_text(tr("暂停", "Paused"), x, y, 245, 248, 252, 245, true);
        draw_button(l.language, language_button_label());
        y += 58.0;
        if (playing_) {
            draw_text_fit(entry_title(playing_->entry), x, y, l.panel.w - 48.0,
                          220, 230, 240, 235); y += 30.0;
            char stats[128];
            const double acc = phigros::engine::compute_score(
                playing_->judge.acc_sum, playing_->judge.max_combo,
                playing_->chart.playable_count).acc_ratio * 100.0;
            if (language_ == Language::ZhCn) {
                std::snprintf(stats, sizeof(stats), "连击 %d  最大 %d  准确率 %.2f%%",
                              playing_->judge.combo, playing_->judge.max_combo, acc);
            } else {
                std::snprintf(stats, sizeof(stats), "Combo %d  Max %d  Accuracy %.2f%%",
                              playing_->judge.combo, playing_->judge.max_combo, acc);
            }
            draw_text_fit(stats, x, y, l.panel.w - 48.0, 160, 178, 196, 220);
        }
        draw_button(l.mode, mode_label());
        draw_button(l.seek_back, "-10s");
        draw_button(l.seek_forward, "+10s");
        draw_button(l.resume, tr("继续", "Resume"));
        draw_button(l.restart, tr("重开", "Restart"));
        draw_button(l.library, tr("曲库", "Library"));
    }

    void draw_result() {
        batch_.draw_rect(0, 0, width_, height_, 0, 0, 0, 145);
        const ResultLayout l = result_layout();
        draw_panel(l.panel, 18, 22, 29, 248);
        const double x = l.panel.x + 26.0;
        const double y = l.panel.y + 24.0;
        draw_text(tr("结算", "Result"), x, y, 245, 248, 252, 245, true);
        draw_button(l.language, language_button_label());
        char score[96];
        char acc[96];
        char combo[96];
        if (language_ == Language::ZhCn) {
            std::snprintf(score, sizeof(score), "分数  %d", last_score_.score);
            std::snprintf(acc, sizeof(acc), "准确率  %.2f%%", last_score_.acc_ratio * 100.0);
            std::snprintf(combo, sizeof(combo), "最大连击  %d", playing_ ? playing_->judge.max_combo : 0);
        } else {
            std::snprintf(score, sizeof(score), "Score  %d", last_score_.score);
            std::snprintf(acc, sizeof(acc), "Accuracy  %.2f%%", last_score_.acc_ratio * 100.0);
            std::snprintf(combo, sizeof(combo), "Max Combo  %d", playing_ ? playing_->judge.max_combo : 0);
        }
        draw_text(score, x + 6.0, y + 72.0);
        draw_text(acc, x + 6.0, y + 106.0);
        draw_text(combo, x + 6.0, y + 140.0);
        const GradeCounts counts = count_grades();
        char grades[128];
        if (language_ == Language::ZhCn) {
            std::snprintf(grades, sizeof(grades), "完美 %d  良好 %d  过早/过晚 %d  漏击 %d",
                          counts.perfect, counts.good, counts.bad, counts.miss);
        } else {
            std::snprintf(grades, sizeof(grades), "Perfect %d  Good %d  Bad %d  Miss %d",
                          counts.perfect, counts.good, counts.bad, counts.miss);
        }
        draw_text(grades, x + 6.0, y + 180.0, 160, 178, 196, 225);
        if (playing_) {
            draw_text_fit(entry_title(playing_->entry), x + 6.0, y + 218.0,
                          l.panel.w - 64.0, 150, 170, 190, 220);
        }
        draw_button(l.replay, tr("重试", "Retry"));
        draw_button(l.detail, tr("详情", "Details"));
        draw_button(l.library, tr("曲库", "Library"));
    }

    static std::string compact_path(const std::string& path) {
        if (path.size() <= 86) return path;
        return path.substr(0, 36) + "..." + path.substr(path.size() - 46);
    }

    void handle_tap(double x, double y) {
        if (has_top_language_button() && top_language_rect().contains(x, y)) {
            toggle_language();
            return;
        }
        switch (screen_) {
            case Screen::Library: handle_library_tap(x, y); break;
            case Screen::Detail: handle_detail_tap(x, y); break;
            case Screen::Settings: handle_settings_tap(x, y); break;
            case Screen::Playing: handle_playing_tap(x, y); break;
            case Screen::Paused: handle_pause_tap(x, y); break;
            case Screen::Result: handle_result_tap(x, y); break;
        }
    }

    void handle_library_tap(double x, double y) {
        const LibraryLayout l = library_layout();
        if (l.settings.contains(x, y)) {
            screen_ = Screen::Settings;
            return;
        }
        if (l.rescan.contains(x, y)) {
            scan_library();
            return;
        }
        if (l.sort.contains(x, y)) {
            sort_mode_ = (sort_mode_ + 1) % 3;
            rebuild_visible_library();
            return;
        }
        if (l.filter.contains(x, y)) {
            filter_mode_ = (filter_mode_ + 1) % 4;
            rebuild_visible_library();
            return;
        }
        const int max_page = max_library_page();
        if (l.prev.contains(x, y) && library_page_ > 0) {
            --library_page_;
            return;
        }
        if (l.next.contains(x, y) && library_page_ < max_page) {
            ++library_page_;
            return;
        }
        if (l.mode.contains(x, y) && selected_ >= 0) {
            autoplay_ = !autoplay_;
            return;
        }
        if (l.details.contains(x, y) && selected_ >= 0) {
            screen_ = Screen::Detail;
            return;
        }
        if (l.play.contains(x, y) && selected_ >= 0) {
            start_playing(selected_);
            return;
        }
        const int start = library_page_ * l.rows;
        for (int row = 0; row < l.rows; ++row) {
            int idx = start + row;
            if (idx >= static_cast<int>(visible_library_.size())) break;
            if (library_row_rect(l, row).contains(x, y)) {
                const int entry_index = visible_library_[idx];
                if (selected_ == entry_index && !l.wide) screen_ = Screen::Detail;
                selected_ = entry_index;
                return;
            }
        }
    }

    void handle_detail_tap(double x, double y) {
        const DetailLayout l = detail_layout();
        if (l.back.contains(x, y)) {
            screen_ = Screen::Library;
            return;
        }
        if (l.play.contains(x, y)) {
            start_playing(selected_);
            return;
        }
        if (l.mode.contains(x, y)) {
            autoplay_ = !autoplay_;
            return;
        }
        if (l.settings.contains(x, y)) {
            screen_ = Screen::Settings;
        }
    }

    void handle_settings_tap(double x, double y) {
        const SettingsLayout l = settings_layout();
        if (l.back.contains(x, y)) {
            screen_ = Screen::Library;
            return;
        }
        if (l.tab_render.contains(x, y)) { settings_tab_ = 0; return; }
        if (l.tab_gameplay.contains(x, y)) { settings_tab_ = 1; return; }
        if (l.tab_fx.contains(x, y)) { settings_tab_ = 2; return; }

        double row_y = l.content.y + 20.0;
        bool changed_visual = false;
        if (settings_tab_ == 0) {
            if (settings_control_rect(l, row_y).contains(x, y)) {
                toggle_language();
                return;
            }
            row_y += l.row_h;
            double bg_dim = static_cast<double>(cfg_.bg_dim);
            if (adjust_slider(x, y, l, row_y, bg_dim, 0.0, 220.0)) {
                cfg_.bg_dim = static_cast<int>(std::round(bg_dim)); return;
            }
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.note_alpha, 0.1, 1.0)) return;
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.approach, 0.5, 8.0)) return;
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.expand_factor, 0.8, 1.4)) return;
            row_y += l.row_h;
            if (settings_control_rect(l, row_y).contains(x, y)) {
                cfg_.note_outline = !cfg_.note_outline;
                changed_visual = true;
            }
        } else if (settings_tab_ == 1) {
            if (settings_control_rect(l, row_y).contains(x, y)) { autoplay_ = !autoplay_; return; }
            row_y += l.row_h;
            if (settings_control_rect(l, row_y).contains(x, y)) { show_gameplay_overlay_ = !show_gameplay_overlay_; return; }
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.chart_speed, 0.5, 3.0)) {
                apply_audio_speed();
                return;
            }
            row_y += l.row_h;
            double hold_fx = static_cast<double>(cfg_.hold_fx_interval_ms);
            if (adjust_slider(x, y, l, row_y, hold_fx, 80.0, 400.0)) {
                cfg_.hold_fx_interval_ms = static_cast<int>(std::round(hold_fx)); return;
            }
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.hold_tail_tol, 0.2, 1.2)) return;
        } else {
            if (settings_control_rect(l, row_y).contains(x, y)) { show_hitfx_ = !show_hitfx_; apply_visual_settings(); return; }
            row_y += l.row_h;
            if (settings_control_rect(l, row_y).contains(x, y)) { show_particles_ = !show_particles_; apply_visual_settings(); return; }
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.hitfx_intensity, 0.0, 2.0)) return;
            row_y += l.row_h;
            double particles = static_cast<double>(cfg_.particle_count);
            if (adjust_slider(x, y, l, row_y, particles, 0.0, 32.0)) {
                cfg_.particle_count = static_cast<int>(std::round(particles));
                apply_visual_settings();
                return;
            }
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.note_scale_x, 1.0, 4.0)) changed_visual = true;
            row_y += l.row_h;
            if (adjust_slider(x, y, l, row_y, cfg_.note_scale_y, 0.5, 2.0)) changed_visual = true;
        }
        if (changed_visual) apply_visual_settings();
    }

    bool adjust_slider(double x, double y, const SettingsLayout& l, double row_y,
                       double& value, double min_v, double max_v) {
        const Rect s = settings_slider_rect(l, row_y);
        if (!Rect{s.x - 10.0, s.y - 8.0, s.w + 20.0, 46.0}.contains(x, y)) return false;
        const double p = std::clamp((x - s.x) / std::max(1.0, s.w), 0.0, 1.0);
        value = min_v + p * (max_v - min_v);
        return true;
    }

    void handle_playing_tap(double x, double y) {
        const PlayLayout l = play_layout();
        if (l.pause.contains(x, y)) {
            toggle_pause();
        } else if (l.restart.contains(x, y)) {
            start_playing(selected_);
        } else if (l.mode.contains(x, y)) {
            autoplay_ = !autoplay_;
        } else if (l.overlay.contains(x, y)) {
            show_gameplay_overlay_ = !show_gameplay_overlay_;
        } else if (l.speed_down.contains(x, y)) {
            cfg_.chart_speed = std::max(0.5, cfg_.chart_speed - 0.1);
            apply_audio_speed();
        } else if (l.speed_up.contains(x, y)) {
            cfg_.chart_speed = std::min(3.0, cfg_.chart_speed + 0.1);
            apply_audio_speed();
        } else if (l.seek.contains(x, y)) {
            seek_ratio((x - l.seek.x) / std::max(1.0, l.seek.w));
        }
    }

    void handle_pause_tap(double x, double y) {
        const PauseLayout l = pause_layout();
        if (l.language.contains(x, y)) {
            toggle_language();
        } else if (l.resume.contains(x, y)) {
            toggle_pause();
        } else if (l.restart.contains(x, y)) {
            start_playing(selected_);
        } else if (l.library.contains(x, y)) {
            unload_chart_audio();
            playing_.reset();
            screen_ = Screen::Library;
        } else if (l.mode.contains(x, y)) {
            autoplay_ = !autoplay_;
        } else if (l.seek_back.contains(x, y)) {
            seek_relative(-10.0);
        } else if (l.seek_forward.contains(x, y)) {
            seek_relative(10.0);
        }
    }

    void handle_result_tap(double x, double y) {
        const ResultLayout l = result_layout();
        if (l.language.contains(x, y)) {
            toggle_language();
        } else if (l.replay.contains(x, y)) {
            start_playing(selected_);
        } else if (l.detail.contains(x, y)) {
            unload_chart_audio();
            playing_.reset();
            screen_ = Screen::Detail;
        } else if (l.library.contains(x, y)) {
            unload_chart_audio();
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
            sync_audio_to_chart_time(playing_ ? playing_->t : 0.0, screen_ == Screen::Playing);
        } else if (screen_ == Screen::Result) {
            unload_chart_audio();
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
            stop_audio_playback();
        } else if (screen_ == Screen::Paused) {
            screen_ = before_pause_;
            if (playing_) playing_->last_wall = now_sec();
            sync_audio_to_chart_time(playing_ ? playing_->t : 0.0, screen_ == Screen::Playing);
        }
    }

    void reset_manual_judge() {
        manual_judge_ = phigros::engine::ManualJudge{};
        manual_judge_.hitfx_color_perfect = respack_.cfg.color_perfect;
        manual_judge_.hitfx_color_good = respack_.cfg.color_good;
        manual_judge_.hitfx_alpha_perfect = respack_.cfg.alpha_perfect;
        manual_judge_.hitfx_alpha_good = respack_.cfg.alpha_good;
    }

    void apply_visual_settings() {
        cfg_.show_hitfx = show_hitfx_;
        cfg_.show_particles = show_particles_;
        note_renderer_.init(width_, height_, cfg_.note_scale_x, cfg_.note_scale_y);
        note_renderer_.note_outline = cfg_.note_outline;
        hold_renderer_.init(width_, height_, cfg_.note_scale_x, cfg_.note_scale_y);
        if (playing_) playing_->effects.particle_count = cfg_.particle_count;
    }

    void apply_audio_speed() {
        if (bgm_loaded_) audio_.set_playback_speed(cfg_.chart_speed);
    }

    void stop_audio_playback() {
        if (bgm_loaded_) audio_.stop();
        bgm_started_ = false;
    }

    void unload_chart_audio() {
        stop_audio_playback();
        audio_.unload_bgm();
        bgm_loaded_ = false;
    }

    void sync_audio_to_chart_time(double chart_t, bool play_now) {
        if (!bgm_loaded_) return;
        const double seek_t = std::max(0.0, chart_t - cfg_.audio_offset_ms / 1000.0);
        audio_.seek(seek_t);
        if (play_now && chart_t >= 0.0) {
            audio_.play();
            bgm_started_ = true;
        } else {
            audio_.stop();
            bgm_started_ = false;
        }
    }

    std::string resolve_chart_audio_path(const ChartEntry& entry, const ChartData& chart) const {
        if (!entry.assets.music_path.empty()) return entry.assets.music_path;
        if (chart.metadata.song_path.empty()) return {};
        if (phigros::chart::is_zip_path(entry.chart_path)) {
            auto [zip_file, member] = phigros::chart::split_zip_path(entry.chart_path);
            (void)member;
            return zip_file + ":" + chart.metadata.song_path;
        }
        fs::path chart_dir = fs::path(entry.chart_path).parent_path();
        return (chart_dir / chart.metadata.song_path).string();
    }

    bool load_chart_audio(const ChartEntry& entry, const ChartData& chart) {
        unload_chart_audio();
        const std::string audio_path = resolve_chart_audio_path(entry, chart);
        if (audio_path.empty()) return false;
        if (!audio_engine_ready_) {
            audio_engine_ready_ = audio_.init();
            if (!audio_engine_ready_) return false;
        }
        bool ok = false;
        if (phigros::chart::is_zip_path(audio_path)) {
            auto [zip_file, member] = phigros::chart::split_zip_path(audio_path);
            auto data = phigros::chart::extract_zip_file(zip_file, member);
            ok = audio_.load_bgm_memory(audio_path, data, chart.offset);
        } else {
            ok = audio_.load_bgm(audio_path, chart.offset);
        }
        bgm_loaded_ = ok;
        if (bgm_loaded_) {
            apply_audio_speed();
            PHLOG_INFO(Audio, "SDL app BGM loaded: " << audio_path);
        } else {
            PHLOG_WARN(Audio, "SDL app failed to load BGM: " << audio_path);
        }
        return bgm_loaded_;
    }

    void start_playing(int index) {
        if (index < 0 || index >= static_cast<int>(library_.size())) return;
        selected_ = index;
        status_kind_ = StatusKind::LoadingChart;
        status_detail_.clear();
        unload_chart_audio();
        playing_ = std::make_unique<PlayingChart>();
        try {
            cfg_.window_w = width_;
            cfg_.window_h = height_;
            cfg_.show_hitfx = show_hitfx_;
            cfg_.show_particles = show_particles_;
            apply_visual_settings();
            reset_manual_judge();
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
            load_chart_audio(playing_->entry, playing_->chart);
            sync_audio_to_chart_time(playing_->t, true);
            screen_ = Screen::Playing;
            status_kind_ = StatusKind::None;
            status_detail_.clear();
        } catch (const std::exception& e) {
            unload_chart_audio();
            status_kind_ = StatusKind::LoadError;
            status_detail_ = e.what();
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
