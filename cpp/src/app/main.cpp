// Main rendering loop integrating all Phase 2 components.
// Usage: phigros_render <chart_path> [--config config.jsonc] [--respack respack.zip]
//                       [--bg background.png] [--font font.ttf] [--audio bgm.mp3]
//                       [--headless] [--screenshot-dir dir] [--duration sec]

#include "phigros/core/types.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/chart/parser.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/hud/hud.hpp"

#include "phigros/app/window.hpp"
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/texture.hpp"
#include "phigros/io/respack.hpp"
#include "phigros/io/audio.hpp"
#include "phigros/render/background.hpp"
#include "phigros/render/line_renderer.hpp"
#include "phigros/render/note_renderer.hpp"
#include "phigros/render/hold_renderer.hpp"
#include "phigros/render/hitfx_renderer.hpp"
#include "phigros/render/hud_renderer.hpp"

#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Detect chart format from file content
static std::string detect_format(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string first_line;
    std::getline(f, first_line);
    // PEC starts with a number
    if (!first_line.empty() && (first_line[0] == 'b' || first_line[0] == 'c' ||
        first_line[0] == 'n' || first_line[0] == '#' ||
        (first_line[0] >= '0' && first_line[0] <= '9')))
        return "pec";
    // Try JSON
    f.seekg(0);
    try {
        json j = json::parse(f);
        if (j.contains("META")) return "rpe";
        return "official";
    } catch (...) {}
    return "pec";
}

static phigros::ChartData load_chart_from_file(const std::string& path,
                                                 const phigros::config::RenderConfig& cfg) {
    std::string fmt = detect_format(path);
    if (fmt == "rpe" || fmt == "official") {
        std::ifstream f(path);
        json j = json::parse(f);
        if (fmt == "rpe")
            return phigros::chart::parse_rpe(j, cfg.window_w, cfg.window_h, cfg.rpe_easing_shift);
        else
            return phigros::chart::parse_official(j, cfg.window_w, cfg.window_h);
    } else {
        return phigros::chart::parse_pec(path, cfg.window_w, cfg.window_h);
    }
}

// Find audio file in chart directory
static std::string find_audio(const std::string& chart_dir) {
    for (auto& ext : {"music.ogg", "music.mp3", "music.wav", "bgm.ogg", "bgm.mp3", "bgm.wav"}) {
        auto p = fs::path(chart_dir) / ext;
        if (fs::exists(p)) return p.string();
    }
    // Try any audio file
    for (auto& entry : fs::directory_iterator(chart_dir)) {
        auto e = entry.path().extension().string();
        if (e == ".ogg" || e == ".mp3" || e == ".wav" || e == ".flac")
            return entry.path().string();
    }
    return "";
}

struct AppArgs {
    std::string chart_path;
    std::string config_path;
    std::string respack_path;
    std::string bg_path;
    std::string font_path;
    std::string audio_path;
    std::string screenshot_dir;
    double duration = 0.0;
    bool headless = false;
    bool score_only = false; // Engine-only, no SDL/rendering
};

static AppArgs parse_args(int argc, char* argv[]) {
    AppArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) args.config_path = argv[++i];
        else if (a == "--respack" && i + 1 < argc) args.respack_path = argv[++i];
        else if (a == "--bg" && i + 1 < argc) args.bg_path = argv[++i];
        else if (a == "--font" && i + 1 < argc) args.font_path = argv[++i];
        else if (a == "--audio" && i + 1 < argc) args.audio_path = argv[++i];
        else if (a == "--screenshot-dir" && i + 1 < argc) args.screenshot_dir = argv[++i];
        else if (a == "--duration" && i + 1 < argc) args.duration = std::atof(argv[++i]);
        else if (a == "--headless") args.headless = true;
        else if (a == "--score-only") { args.score_only = true; args.headless = true; }
        else if (args.chart_path.empty()) args.chart_path = a;
    }
    return args;
}

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);
    if (args.chart_path.empty()) {
        std::cerr << "Usage: phigros_render <chart_path> [options]\n"
                  << "  --config <path>         Render config JSONC\n"
                  << "  --respack <path>        Respack ZIP file\n"
                  << "  --bg <path>             Background image\n"
                  << "  --font <path>           TTF font file\n"
                  << "  --audio <path>          BGM audio file\n"
                  << "  --screenshot-dir <dir>  Save frames as BMP\n"
                  << "  --duration <sec>        Auto-quit after N seconds\n"
                  << "  --headless              No visible window\n";
        return 1;
    }

    // Load config
    phigros::config::RenderConfig cfg;
    if (!args.config_path.empty()) {
        cfg = phigros::config::load_config(args.config_path);
    }
    int W = cfg.window_w, H = cfg.window_h;

    // Load chart
    std::cout << "[Chart] Loading: " << args.chart_path << std::endl;
    auto chart = load_chart_from_file(args.chart_path, cfg);
    std::cout << "[Chart] Lines=" << chart.lines.size()
              << " Notes=" << chart.notes.size()
              << " Offset=" << chart.offset << "s" << std::endl;

    // Precompute visibility
    phigros::engine::precompute_t_enter(chart.lines, chart.notes, W, H);

    // Init note states
    std::vector<phigros::NoteState> states(chart.notes.size());
    for (size_t i = 0; i < chart.notes.size(); ++i)
        states[i].note = &chart.notes[i];

    // Init engine components
    phigros::engine::Judge judge;

    phigros::engine::SimulatePlayer autoplay(phigros::engine::SimMode::Conservative);

    phigros::engine::EffectManager effects;
    int idx_next = 0; // scanning cursor for note processing

    // Chart end time
    double chart_end = 0.0;
    for (auto& n : chart.notes) chart_end = std::max(chart_end, n.t_end);
    chart_end += 2.0; // Extra time after last note

    // Init SDL window
    phigros::app::Window window;
    window.init(W, H, "Phigros Renderer", args.headless);
    std::cout << "[Window] " << W << "x" << H
              << (args.headless ? " (headless)" : "") << std::endl;

    // Init sprite batch
    phigros::render::SpriteBatch batch;
    batch.init(window.ren);

    // Load respack
    std::string respack_path = args.respack_path.empty() ? cfg.respack_path : args.respack_path;
    phigros::io::Respack respack = phigros::io::load_respack(window.ren, respack_path);
    std::cout << "[Respack] " << (respack.loaded ? "Loaded" : "Fallback")
              << " (" << respack_path << ")" << std::endl;

    // Load background
    phigros::render::BackgroundRenderer bg_renderer;
    std::string bg_path = args.bg_path.empty() ? cfg.bg_path : args.bg_path;
    if (!bg_path.empty()) {
        bg_renderer.load(window.ren, bg_path, W, H, cfg.bg_blur);
        std::cout << "[Background] " << (bg_renderer.has_bg ? "Loaded" : "Failed") << std::endl;
    }

    // Init renderers
    phigros::render::LineRenderer line_renderer;
    line_renderer.line_w = std::max(2.0, H * 0.005);
    line_renderer.dot_r = std::max(3.0, H * 0.007);

    phigros::render::NoteRenderer note_renderer;
    note_renderer.init(W, H, cfg.note_scale_x, cfg.note_scale_y);

    phigros::render::HoldRenderer hold_renderer;
    hold_renderer.init(W, H, cfg.note_scale_x, cfg.note_scale_y);

    phigros::render::HitFXRenderer hitfx_renderer;

    phigros::render::HudRenderer hud_renderer;
    if (!args.font_path.empty()) {
        hud_renderer.init(window.ren, args.font_path, W, H);
    } else {
        hud_renderer.init(window.ren, "", W, H);
    }
    hud_renderer.screen_w = W;
    hud_renderer.screen_h = H;

    // Init audio
    phigros::io::AudioSystem audio;
    bool has_audio = false;
    std::string audio_path = args.audio_path;
    if (audio_path.empty()) {
        // Try to find audio in chart directory
        auto chart_dir = fs::path(args.chart_path).parent_path().string();
        audio_path = find_audio(chart_dir);
    }
    if (!audio_path.empty()) {
        if (audio.init()) {
            has_audio = audio.load_bgm(audio_path, chart.offset);
            if (has_audio) std::cout << "[Audio] Loaded: " << audio_path << std::endl;
        }
    }

    // Screenshot setup
    if (!args.screenshot_dir.empty()) {
        fs::create_directories(args.screenshot_dir);
    }
    int screenshot_counter = 0;

    // Timing
    double t = -1.0; // Start 1 second before chart
    double dt_frame = 1.0 / 60.0;
    double last_time = phigros::app::Window::get_time_sec();
    double fps_sum = 0;
    int fps_count = 0;
    double fps_display = 60.0;
    bool started_audio = false;

    int playable_notes = 0;
    for (auto& n : chart.notes) if (!n.fake) ++playable_notes;

    // Binary search helper for idx_next
    auto find_idx_next = [&](double t) -> int {
        int lo = 0, hi = static_cast<int>(states.size());
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (states[mid].judged || states[mid].note->t_hit < t - 0.5)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    };

    // === SCORE-ONLY MODE: pure engine, no SDL ===
    if (args.score_only) {
        constexpr double SIM_DT = 1.0 / 240.0;
        constexpr double HOLD_TAIL_TOL = 0.30;
        double t = chart.offset;
        for (; t <= chart_end; t += SIM_DT) {
            autoplay.step(t, chart.notes, states, chart.lines, judge, W, H);
            idx_next = find_idx_next(t);
            phigros::engine::detect_misses(states, idx_next, t,
                                           phigros::engine::Judge::BAD, judge);
            phigros::engine::hold_maintenance(states, idx_next, t,
                                              HOLD_TAIL_TOL, judge);
            phigros::engine::hold_finalize(states, idx_next, t,
                                           HOLD_TAIL_TOL,
                                           phigros::engine::Judge::BAD, judge);
        }
        auto sr = phigros::engine::compute_score(
            judge.acc_sum, judge.max_combo, playable_notes);
        std::cout << "\n=== Score Only ===" << std::endl;
        std::cout << "Score: " << sr.score << std::endl;
        std::cout << "Accuracy: " << (sr.acc_ratio * 100.0) << "%" << std::endl;
        std::cout << "MaxCombo: " << judge.max_combo << "/" << playable_notes << std::endl;
        std::cout << "Judged: " << judge.judged_cnt << "/" << playable_notes << std::endl;
        return (sr.score == 1000000) ? 0 : 1;
    }

    std::cout << "[Render] Starting main loop (chart_end=" << chart_end << "s, "
              << playable_notes << " playable notes)" << std::endl;

    // Headless: decouple sim rate (240fps) from render rate (60fps)
    constexpr double SIM_DT = 1.0 / 240.0;
    constexpr int SIM_STEPS_PER_RENDER = 4; // 240/60
    int headless_sub = 0;

    // === MAIN LOOP ===
    while (!window.quit_requested) {
        // In headless mode, only do event poll/timing on render frames
        if (!args.headless || headless_sub == 0) {
            double now = phigros::app::Window::get_time_sec();
            dt_frame = std::min(now - last_time, 0.1);
            last_time = now;

            fps_sum += dt_frame;
            fps_count++;
            if (fps_sum >= 0.5) {
                fps_display = fps_count / fps_sum;
                fps_sum = 0; fps_count = 0;
            }

            window.poll_events();
        }

        // Advance time
        if (has_audio && started_audio) {
            t = audio.get_playback_time();
        } else if (args.headless) {
            t += SIM_DT;
        } else {
            t += dt_frame;
        }

        // Start audio after countdown
        if (has_audio && !started_audio && t >= 0.0) {
            audio.play();
            started_audio = true;
        }

        // Auto-quit conditions
        if (args.duration > 0 && t >= args.duration) break;
        if (t > chart_end) break;
        if (has_audio && started_audio && audio.is_at_end() && t > 1.0) break;

        // === ENGINE UPDATE (every sim tick) ===
        autoplay.step(t, chart.notes, states, chart.lines, judge, W, H);

        if (t >= 0.0) {
            idx_next = find_idx_next(t);
            phigros::engine::detect_misses(states, idx_next, t,
                                           phigros::engine::Judge::BAD, judge);
            phigros::engine::hold_maintenance(states, idx_next, t,
                                              cfg.hold_tail_tol, judge);
            phigros::engine::hold_finalize(states, idx_next, t,
                                           cfg.hold_tail_tol,
                                           phigros::engine::Judge::BAD, judge);
            effects.hold_tick_fx(states, idx_next, t,
                                 cfg.hold_fx_interval_ms, chart.lines);
            effects.update(t, t * 1000.0, respack.cfg.hitfx_duration);
        }

        // Skip rendering on intermediate sim ticks in headless mode
        if (args.headless && ++headless_sub < SIM_STEPS_PER_RENDER) continue;
        headless_sub = 0;

        // === BUILD FRAME SNAPSHOT ===
        auto frame = phigros::render::build_frame(
            t, chart, states, judge, cfg);

        // === RENDER ===
        window.begin_frame();
        bg_renderer.draw(batch, cfg.bg_dim);
        hold_renderer.draw(batch, respack, frame.notes, t);
        line_renderer.draw(batch, respack.white_tex, frame.lines, W, H, cfg.expand_factor);
        note_renderer.draw(batch, respack, frame.notes, t);
        hitfx_renderer.draw(batch, respack, effects, t);
        hud_renderer.draw(batch, frame.hud, fps_display);
        window.end_frame();

        // Screenshot
        if (!args.screenshot_dir.empty()) {
            double interval = 5.0;
            int expected = static_cast<int>(t / interval);
            if (expected >= screenshot_counter && t >= 0.0) {
                char fname[256];
                std::snprintf(fname, sizeof(fname), "%s/frame_%04d_t%.2f.bmp",
                    args.screenshot_dir.c_str(), screenshot_counter, t);
                window.save_screenshot(fname);
                screenshot_counter = expected + 1;
            }
        }
    }

    // Final results
    auto sr = phigros::engine::compute_score(
        judge.acc_sum, judge.max_combo, playable_notes);
    std::cout << "\n=== Render Complete ===" << std::endl;
    std::cout << "Score: " << sr.score << std::endl;
    std::cout << "Accuracy: " << (sr.acc_ratio * 100.0) << "%" << std::endl;
    std::cout << "MaxCombo: " << judge.max_combo << "/" << playable_notes << std::endl;
    std::cout << "Judged: " << judge.judged_cnt << "/" << playable_notes << std::endl;

    // Cleanup
    audio.destroy();
    respack.destroy();
    bg_renderer.destroy();
    hud_renderer.destroy();
    window.destroy();

    return (sr.score == 1000000) ? 0 : 1;
}
