#pragma once
// GameLoop: owns all mutable chart/simulation state and drives the per-frame
// update-render cycle. Replaces the former main_loop_body lambda.
//
// Usage:
//   GameLoop gl(ctx, args, cfg, chart, playable_notes, chart_end);
//   while (gl.run_frame()) {}  // desktop
//   emscripten_set_main_loop_arg(GameLoop::wasm_tick, &gl, 0, 1);  // WASM

#include "phigros/app/app_args.hpp"
#include "phigros/app/app_context.hpp"
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/engine/manual_judge.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/render/render_target.hpp"
#include "phigros/render/result_screen.hpp"
#include "phigros/render/pause_overlay.hpp"
#include "phigros/io/replay.hpp"
#include "phigros/io/video_encoder.hpp"

#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <iomanip>

#ifdef PHIGROS_WASM
#include <emscripten.h>
#endif
#include <iostream>
#include <cmath>

namespace phigros::app {

struct GameLoop {
    // ── External references ──────────────────────────────────────────────────
    AppContext&                        ctx;
    const AppArgs&                     args;
    const config::RenderConfig&        cfg;
    const ChartData&                   chart;
    const int                          W, H;
    const int                          playable_notes;
    const double                       chart_end;
    const bool                         is_play_mode;
    const double                       audio_offset_sec;  // cfg.audio_offset_ms/1000

    // ── Simulation state ─────────────────────────────────────────────────────
    std::vector<NoteState>             states;
    engine::Judge                      judge;
    engine::EffectManager              effects;
    engine::SimulatePlayer             autoplay{engine::SimMode::Conservative};
    engine::ManualJudge                manual_judge;
    io::ReplayWriter                   replay_writer;
    io::ReplayPlayer                   replay_player;

    // ── Frame / timing state ─────────────────────────────────────────────────
    double  t            = -1.0;
    double  dt_frame     = 1.0 / 60.0;
    double  last_time    = 0.0;
    double  fps_sum      = 0.0;
    int     fps_count    = 0;
    double  fps_display  = 60.0;
    bool    paused       = false;
    bool    result_shown = false;
    double  result_t     = 0.0;
    int     idx_next     = 0;
    int     headless_sub = 0;

    // ── Recording ────────────────────────────────────────────────────────────
    io::RecordingSession    recorder;
    bool                    is_recording = false;
    std::vector<uint8_t>    readback_rgba;
    int                     record_log_frames = 0;

    // ── Screenshot ───────────────────────────────────────────────────────────
    int screenshot_counter = 0;

    // ── UI helpers ───────────────────────────────────────────────────────────
    render::ResultScreen result_screen;
    render::PauseOverlay pause_overlay;

    // ── Per-frame profiling ───────────────────────────────────────────────────
    // Tracks wall-clock time for 5 render phases. Enabled by --profile flag.
    // Prints rolling stats every ~60 rendered frames.
    struct FramePhaseStats {
        uint64_t freq = 1;
        static constexpr int PHASES = 5;
        // 0=build_frame, 1=render_scene, 2=trail_blur, 3=readback, 4=present
        static const char* phase_name(int i) {
            static const char* names[PHASES] = {
                "build_frame", "render_scene", "trail_blur ", "readback   ", "present    "
            };
            return names[i];
        }

        double sum_ms[PHASES]  = {};
        double max_ms[PHASES]  = {};
        double sum2_ms[PHASES] = {};  // for p95 via sorted reservoir
        static constexpr int HIST = 128;
        double hist[PHASES][HIST] = {};
        int    hist_n = 0;
        int    report_frame = 0;
        static constexpr int REPORT_INTERVAL = 60;

        void init() { freq = SDL_GetPerformanceFrequency(); }

        uint64_t now() const { return SDL_GetPerformanceCounter(); }

        double to_ms(uint64_t ticks) const {
            return static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq);
        }

        void record(int phase, uint64_t start, uint64_t end) {
            double ms = to_ms(end - start);
            sum_ms[phase]  += ms;
            sum2_ms[phase] += ms * ms;
            if (ms > max_ms[phase]) max_ms[phase] = ms;
            if (hist_n < HIST) {
                hist[phase][hist_n] = ms;
            }
        }

        // Call at end of each rendered frame; returns true when a report was printed.
        bool end_frame(bool enabled) {
            ++hist_n;
            ++report_frame;
            if (!enabled || report_frame < REPORT_INTERVAL) return false;
            int n = report_frame;
            std::cout << "[Profile] " << n << " frames";
            for (int p = 0; p < PHASES; ++p) {
                double mean = sum_ms[p] / n;
                // p95 approximation from histogram
                int hsz = std::min(hist_n, HIST);
                std::sort(hist[p], hist[p] + hsz);
                double p95 = hsz > 0 ? hist[p][static_cast<int>(hsz * 0.95)] : 0.0;
                std::cout << "  |" << phase_name(p)
                          << " avg=" << std::fixed << std::setprecision(2) << mean
                          << "ms p95=" << p95 << "ms max=" << max_ms[p] << "ms";
            }
            std::cout << "\n";
            // Reset
            for (int p = 0; p < PHASES; ++p) {
                sum_ms[p] = sum2_ms[p] = max_ms[p] = 0.0;
                std::fill(std::begin(hist[p]), std::end(hist[p]), 0.0);
            }
            hist_n = 0;
            report_frame = 0;
            return true;
        }
    } prof;

    // ── Derived constants ────────────────────────────────────────────────────
    static constexpr double SIM_DT = 1.0 / 240.0;
    double render_dt         = 1.0 / 60.0;
    int    sim_steps_per_render = 1;

    // ────────────────────────────────────────────────────────────────────────
    GameLoop(AppContext& ctx_, const AppArgs& args_,
             const config::RenderConfig& cfg_,
             const ChartData& chart_,
             int playable_notes_, double chart_end_)
        : ctx(ctx_), args(args_), cfg(cfg_), chart(chart_)
        , W(cfg_.window_w), H(cfg_.window_h)
        , playable_notes(playable_notes_), chart_end(chart_end_)
        , is_play_mode(args_.play_mode || !args_.play_replay_path.empty())
        , audio_offset_sec(cfg_.audio_offset_ms / 1000.0)
    {
        // Init note states
        states.resize(chart.notes.size());
        for (size_t i = 0; i < chart.notes.size(); ++i)
            states[i].note = &chart.notes[i];

        // Propagate config to EffectManager
        effects.particle_count = cfg.particle_count;
        manual_judge.hitfx_color_perfect = ctx.respack.cfg.color_perfect;
        manual_judge.hitfx_color_good = ctx.respack.cfg.color_good;

        // Timing constants
        render_dt = args.record_output.empty() ? (1.0 / 60.0)
                                               : (1.0 / args.record_fps);
        sim_steps_per_render = std::max(1, static_cast<int>(
            std::round(render_dt / SIM_DT)));

        last_time = Window::get_time_sec();

        // Load replay if requested
        if (!args.play_replay_path.empty()) {
            if (!replay_player.load(args.play_replay_path)) {
                std::cerr << "[Replay] Failed to load: " << args.play_replay_path << "\n";
            } else {
                std::cout << "[Replay] Loaded: " << args.play_replay_path
                          << " (" << replay_player.events.size() << " events)\n";
            }
        }

        // Start recording if requested
        is_recording = !args.record_output.empty();
        if (is_recording) {
            io::RecordConfig rc;
            rc.output     = args.record_output;
            rc.preset_name= args.record_preset;
            rc.codec      = args.record_codec;
            rc.hw_type    = args.record_hw;
            rc.fps        = args.record_fps;
            rc.width      = args.record_w;
            rc.height     = args.record_h;
            rc.capture_width = args.record_capture_w;
            rc.capture_height = args.record_capture_h;
            rc.queue_depth = args.record_queue_depth;
            rc.start_time = args.record_start;
            rc.end_time   = args.record_end;
            rc.audio_path = args.audio_path;
            if (rc.audio_path.empty())
                rc.audio_path = find_chart_audio(
                    std::filesystem::path(args.chart_path).parent_path().string());
            if (!recorder.start(rc, W, H)) {
                std::cerr << "[Record] Failed to start recording\n";
                is_recording = false;
            } else {
                readback_rgba.resize(static_cast<size_t>(W) * H * 4);
            }
        }

        // Screenshot directory
        if (!args.screenshot_dir.empty())
            std::filesystem::create_directories(args.screenshot_dir);

        prof.init();
    }

    // ── run_frame() ──────────────────────────────────────────────────────────
    // Returns true to keep running, false to exit the loop.
    bool run_frame() {
        // === 1. TIMING + EVENTS (on render frames only in headless mode) ===
        if (!args.headless || headless_sub == 0) {
            double now = Window::get_time_sec();
            dt_frame = std::min(now - last_time, 0.1);
            last_time = now;
            fps_sum += dt_frame; ++fps_count;
            if (fps_sum >= 0.5) {
                fps_display = fps_count / fps_sum;
                fps_sum = 0; fps_count = 0;
            }

            ctx.window.poll_events();

            if (is_play_mode) {
                ctx.input.begin_frame();
                for (const auto& e : ctx.window.last_events) {
                    ctx.input.process_event(e, W, H);
                    if (e.type == PHIGROS_SDL_EVENT_KEY_DOWN) {
                        auto sc = PHIGROS_KEY_SCANCODE(e);
                        if (sc == SDL_SCANCODE_SPACE) paused = !paused;
                        if (sc == SDL_SCANCODE_R)     do_restart();
                    }
                }
                ctx.input.end_frame(dt_frame);
            }
        }

        // === 2. PAUSED: re-render frozen frame + overlay, no engine tick ===
        if (paused) {
            auto frame = render::build_frame(t, chart, states, judge, cfg);
            ctx.window.begin_frame();
            ctx.bg.draw(ctx.batch, cfg.bg_dim);
            render_scene_at(t, frame);
            ctx.hud_ren.draw(ctx.batch, frame.hud, fps_display);
            pause_overlay.draw(ctx.batch, ctx.hud_ren, W, H);
            ctx.window.end_frame();
            return !ctx.window.quit_requested;
        }

        // === 3. ADVANCE TIME ===
        if (ctx.has_audio && ctx.started_audio) {
            t = ctx.audio.get_playback_time() + audio_offset_sec;
        } else if (args.headless) {
            t += SIM_DT;
        } else {
            t += dt_frame;
        }

        if (ctx.has_audio && !ctx.started_audio && t >= 0.0) {
            ctx.audio.play();
            ctx.started_audio = true;
        }

        // === 4. EXIT CONDITIONS ===
        if (args.duration > 0 && t >= args.duration) return false;
        if (t > chart_end) {
            if (is_play_mode && !result_shown) mark_result();
            if (!is_play_mode) return false;
        }
        if (result_shown && Window::get_time_sec() - result_t > 5.0) return false;
        if (ctx.has_audio && ctx.started_audio && ctx.audio.is_at_end() && t > 1.0) {
            if (is_play_mode && !result_shown) mark_result();
            if (!is_play_mode) return false;
        }

        // === 5. ENGINE UPDATE ===
        if (is_play_mode) {
            if (!result_shown && t >= 0.0) {
                if (replay_player.enabled()) {
                    replay_player.tick(t, chart.notes, states, judge,
                        [&](int nidx, float ft, const std::string& g) {
                            if (nidx < 0 || nidx >= static_cast<int>(chart.notes.size())) return;
                            const auto& note = chart.notes[nidx];
                            math::RGB col = resolve_hitfx_color(note, g);
                            for (const auto& ns : render::build_frame(
                                     ft, chart, states, judge, cfg).notes) {
                                if (ns.nid == nidx) {
                                    effects.add_hitfx(ns.wx, ns.wy, ft, col);
                                    effects.add_particle_burst(ns.wx, ns.wy,
                                        ft * 1000.0, 500.0, col);
                                    break;
                                }
                            }
                        });
                } else {
                    auto mj_frame = render::build_frame(t, chart, states, judge, cfg);
                    manual_judge.on_judgment = [&](int nidx, float ft, const std::string& g) {
                        if (!args.save_replay_path.empty())
                            replay_writer.record(ft, (uint32_t)nidx, g);
                    };
                    manual_judge.process_frame(ctx.input, mj_frame,
                        chart.notes, states, judge, effects, t, W, H);
                    ctx.input.flush_released();
                }
            }
        } else {
            static std::vector<int> s_hit_notes;
            s_hit_notes.clear();
            autoplay.step(t, chart.notes, states, chart.lines, judge, W, H, &s_hit_notes);
            // Emit hit effects for each note judged this step (mirrors Python autoplay)
            if (t >= 0.0 && cfg.show_hitfx) {
                for (int i : s_hit_notes) {
                    const auto& n = chart.notes[i];
                    if (n.line_id < 0 || n.line_id >= static_cast<int>(chart.lines.size())) continue;
                    auto ls = engine::eval_line_state(chart.lines[n.line_id], t,
                        cfg.force_line_alpha01,
                        cfg.force_line_alpha01_by_lid ? &(*cfg.force_line_alpha01_by_lid) : nullptr);
                    auto pos = engine::note_world_pos_cs(
                        ls.x, ls.y, ls.cos_rot, ls.sin_rot, ls.scroll, n,
                        n.scroll_hit, false, cfg.note_flow_speed_multiplier,
                        cfg.note_speed_mul_affects_travel, n.kind == 3);  // holds clamp to line
                    auto g = judge.grade_window(n.t_hit, t).value_or("PERFECT");
                    auto col = resolve_hitfx_color(n, g);
                    effects.add_hitfx(pos.x, pos.y, t, col);
                    if (cfg.show_particles)
                        effects.add_particle_burst(pos.x, pos.y, t * 1000.0,
                            ctx.respack.cfg.hitfx_duration * 1000.0, col);
                }
            }
        }

        if (t >= 0.0) {
            idx_next = find_idx_next(t);
            engine::detect_misses(states, idx_next, t,
                                  engine::Judge::BAD, judge);
            engine::hold_maintenance(states, idx_next, t,
                                     cfg.hold_tail_tol, judge);
            engine::hold_finalize(states, idx_next, t, cfg.hold_tail_tol,
                                  engine::Judge::BAD, judge);
            effects.hold_tick_fx(states, idx_next, t,
                                 cfg.hold_fx_interval_ms, chart.lines,
                                 ctx.respack.cfg.color_perfect);
            effects.update(t, t * 1000.0, ctx.respack.cfg.hitfx_duration);
        }

        // === 6. SKIP RENDER ON INTERMEDIATE SIM TICKS ===
        if (args.headless && ++headless_sub < sim_steps_per_render) return true;
        headless_sub = 0;

        // === 7. BUILD FRAME SNAPSHOT ===
        uint64_t t0_build = prof.now();
        auto frame = render::build_frame(t, chart, states, judge, cfg);
        uint64_t t1_build = prof.now();

        // === 8. RENDER ===
        ctx.window.begin_frame();

        uint64_t t0_trail = prof.now();
        if (ctx.motion_blur.enabled()) {
            ctx.bg.draw(ctx.batch, cfg.bg_dim);
            ctx.motion_blur.begin_accumulate(ctx.window.ren);
            double dt_chart = dt_frame * cfg.chart_speed;
            for (int i = 0; i < ctx.motion_blur.samples; ++i) {
                double t_sub = ctx.motion_blur.sample_time(t, dt_chart, i);
                ctx.motion_blur.begin_subframe(ctx.window.ren);
                auto sub_fr = (t_sub == t) ? frame
                    : render::build_frame(t_sub, chart, states, judge, cfg);
                uint64_t t0_scene = prof.now();
                render_scene_at(t_sub, sub_fr);
                prof.record(1, t0_scene, prof.now());
                ctx.motion_blur.add_subframe(ctx.window.ren, ctx.motion_blur.sample_weight(i));
            }
            ctx.motion_blur.composite(ctx.window.ren);
        } else if (ctx.trail.enabled()) {
            ctx.trail.begin_frame(ctx.window.ren);
            uint64_t t0_scene = prof.now();
            render_scene_at(t, frame);
            prof.record(1, t0_scene, prof.now());
            render::RenderTarget::unbind(ctx.window.ren);
            ctx.bg.draw(ctx.batch, cfg.bg_dim);
            ctx.trail.composite(ctx.window.ren);
        } else {
            ctx.bg.draw(ctx.batch, cfg.bg_dim);
            uint64_t t0_scene = prof.now();
            render_scene_at(t, frame);
            prof.record(1, t0_scene, prof.now());
        }
        uint64_t t1_trail = prof.now();

        ctx.hud_ren.draw(ctx.batch, frame.hud, fps_display);

        // Result overlay (play/replay mode)
        if (result_shown) {
            double elapsed = Window::get_time_sec() - result_t;
            auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, playable_notes);
            result_screen.draw(ctx.batch, ctx.hud_ren, judge, sr,
                               playable_notes, W, H, elapsed * 1.5);
        }

        uint64_t t0_present = prof.now();
        ctx.window.end_frame();
        uint64_t t1_present = prof.now();

        // === 9. VIDEO CAPTURE ===
        uint64_t t0_readback = prof.now();
        if (is_recording) do_capture();
        uint64_t t1_readback = prof.now();

        // === 10. SCREENSHOT ===
        if (!args.screenshot_dir.empty()) do_screenshot();

        // Record profiling stats
        prof.record(0, t0_build,    t1_build);
        prof.record(2, t0_trail,    t1_trail);
        prof.record(3, t0_readback, t1_readback);
        prof.record(4, t0_present,  t1_present);
        prof.end_frame(args.profile);

        return !ctx.window.quit_requested;
    }

    // ── Public accessors for post-loop reporting ──────────────────────────────
    engine::ScoreResult final_score() const {
        return engine::compute_score(judge.acc_sum, judge.max_combo, playable_notes);
    }

    void finish() {
        if (is_recording) {
            std::cout << "\n";
            recorder.finish();
        }
        if (!args.save_replay_path.empty() && !replay_writer.events.empty()) {
            uint32_t hash = io::chart_path_hash(args.chart_path);
            if (replay_writer.save(args.save_replay_path, hash))
                std::cout << "[Replay] Saved: " << args.save_replay_path
                          << " (" << replay_writer.events.size() << " events)\n";
            else
                std::cerr << "[Replay] Save failed: " << args.save_replay_path << "\n";
        }
    }

    // ── Emscripten trampoline ─────────────────────────────────────────────────
    static void wasm_tick(void* arg) {
        auto* gl = static_cast<GameLoop*>(arg);
        if (!gl->run_frame()) {
#ifdef PHIGROS_WASM
            emscripten_cancel_main_loop();
#endif
        }
    }

private:
    // ── Helpers ───────────────────────────────────────────────────────────────

    void mark_result() {
        result_shown = true;
        result_t     = Window::get_time_sec();
    }

    // Binary search: first note index near t that might still need judgment.
    int find_idx_next(double cur_t) const {
        int lo = 0, hi = static_cast<int>(states.size());
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (states[mid].judged || states[mid].note->t_hit < cur_t - 0.5)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    math::RGB resolve_hitfx_color(const Note& note, const std::string& grade) const {
        if (note.tint_hitfx_rgb) return *note.tint_hitfx_rgb;
        if (grade == "GOOD" || grade == "BAD") return ctx.respack.cfg.color_good;
        return ctx.respack.cfg.color_perfect;
    }

    // Record into DrawList and execute via SdlExecutor.
    void render_scene_at(double t_r, const render::FrameSnapshot& fr) {
        // Adaptive reserve: reuse last-frame command count to avoid realloc
        static thread_local size_t s_last_dl_sz = 256;
        ctx.draw_list.clear();
        ctx.draw_list.cmds.reserve(s_last_dl_sz + 32);
        ctx.batch.dl = &ctx.draw_list;
        ctx.hold_ren.draw(ctx.batch, ctx.respack, fr.notes, t_r, W, H, cfg.expand_factor);
        ctx.line_ren.draw(ctx.batch, ctx.respack.white_tex, fr.lines, W, H, cfg.expand_factor);
        ctx.note_ren.draw(ctx.batch, ctx.respack, fr.notes, t_r, W, H, cfg.expand_factor);
        ctx.hitfx_ren.draw(ctx.batch, ctx.respack, effects, t_r,
                           cfg.show_hitfx, cfg.show_particles,
                           static_cast<float>(cfg.hitfx_intensity),
                           W, H, cfg.expand_factor);
        s_last_dl_sz = ctx.draw_list.cmds.size();
        ctx.batch.dl = nullptr;
        render::SdlExecutor::execute(ctx.window.ren, ctx.draw_list);
    }

    void do_restart() {
        t = -1.0;
        paused = false;
        result_shown = false;
        for (size_t i = 0; i < states.size(); ++i) {
            states[i] = NoteState{};
            states[i].note = &chart.notes[i];
        }
        judge         = engine::Judge{};
        effects       = engine::EffectManager{};
        effects.particle_count = cfg.particle_count;
        manual_judge  = engine::ManualJudge{};
        manual_judge.hitfx_color_perfect = ctx.respack.cfg.color_perfect;
        manual_judge.hitfx_color_good = ctx.respack.cfg.color_good;
        replay_player.cursor = 0;
        ctx.reload_audio(chart.offset);
    }

    void do_capture() {
        if (args.record_start > -0.5 && t < args.record_start) return;
        if (args.record_end > 0.0 && t > args.record_end) {
            recorder.finish();
            is_recording = false;
            return;
        }
        ctx.window.read_pixels_rgba(readback_rgba.data());
        if (!recorder.capture_rgba(readback_rgba.data(), W, H)) {
            std::cerr << "[Record] Capture failed, stopping recorder\n";
            recorder.finish();
            is_recording = false;
            return;
        }
        if (++record_log_frames % static_cast<int>(args.record_fps) == 0)
            recorder.log_progress(t, chart_end);
    }

    void do_screenshot() {
        double fps = (args.screenshot_fps > 0.0) ? args.screenshot_fps : 0.2;
        double interval = 1.0 / fps;
        int expected = static_cast<int>(t / interval);
        if (expected >= screenshot_counter && t >= 0.0) {
            char fname[256];
            std::snprintf(fname, sizeof(fname), "%s/frame_%04d_t%.2f.png",
                args.screenshot_dir.c_str(), screenshot_counter, t);
            ctx.window.save_screenshot_png(fname);
            screenshot_counter = expected + 1;
        }
    }
};

} // namespace phigros::app
