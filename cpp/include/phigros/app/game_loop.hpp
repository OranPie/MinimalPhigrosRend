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
#include <deque>
#include <fstream>
#include <iterator>
#include <sstream>
#include <cmath>
#include <cinttypes>
#include <unordered_map>

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
    const double                       progress_end;
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
    std::deque<std::string> recent_judges;
    std::deque<double>      debug_frame_ms_hist;
    std::unordered_map<int, const Note*> debug_note_by_id;
    std::unordered_map<int, std::pair<double, double>> debug_prev_note_pos;
    std::unordered_map<int, std::deque<std::pair<double, double>>> debug_note_trails;
    std::unordered_map<int, std::pair<double, double>> debug_prev_line_pos;
    std::unordered_map<int, double> debug_prev_line_scroll;
    bool mirror_mod_active = false;

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
            std::ostringstream oss;
            oss << n << " frames";
            for (int p = 0; p < PHASES; ++p) {
                double mean = sum_ms[p] / n;
                // p95 approximation from histogram
                int hsz = std::min(hist_n, HIST);
                std::sort(hist[p], hist[p] + hsz);
                double p95 = hsz > 0 ? hist[p][static_cast<int>(hsz * 0.95)] : 0.0;
                oss << "  |" << phase_name(p)
                    << " avg=" << std::fixed << std::setprecision(2) << mean
                    << "ms p95=" << p95 << "ms max=" << max_ms[p] << "ms";
            }
            PHLOG_INFO(Profile, oss.str());
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
    double sim_dt            = 1.0 / 240.0;
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
        , progress_end((args_.duration > 0.0) ? std::min(chart_end_, args_.duration) : chart_end_)
        , is_play_mode(args_.play_mode || !args_.play_replay_path.empty())
        , audio_offset_sec(cfg_.audio_offset_ms / 1000.0)
    {
        // Init note states
        states.resize(chart.notes.size());
        for (size_t i = 0; i < chart.notes.size(); ++i) {
            states[i].note = &chart.notes[i];
            debug_note_by_id[chart.notes[i].nid] = &chart.notes[i];
        }
        for (const auto& mp : args.mod_paths) {
            if (mp.find("mirror") != std::string::npos) {
                mirror_mod_active = true;
                break;
            }
            std::ifstream ifs(mp);
            if (!ifs) continue;
            std::string raw((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
            if (raw.find("\"type\"") != std::string::npos &&
                raw.find("mirror") != std::string::npos) {
                mirror_mod_active = true;
                break;
            }
        }

        // Propagate config to EffectManager
        effects.particle_count = cfg.particle_count;
        manual_judge.hitfx_color_perfect = ctx.respack.cfg.color_perfect;
        manual_judge.hitfx_color_good = ctx.respack.cfg.color_good;
        autoplay = engine::SimulatePlayer(parse_sim_mode(cfg.simulateplay.mode),
                                          cfg.simulateplay.max_pointers);
        autoplay.set_humanize(cfg.simulateplay.enabled, cfg.simulateplay.jitter_ms);
        autoplay.set_visuals(cfg.simulateplay.enabled && cfg.simulateplay.render_pointer,
                             cfg.simulateplay.enabled && cfg.simulateplay.render_trail,
                             cfg.simulateplay.trail_seconds,
                             cfg.simulateplay.cursor_radius_px);

        // Timing constants
        sim_dt = 1.0 / std::max(1.0, args.sim_fps);
        render_dt = args.record_output.empty() ? (1.0 / 60.0)
                                               : (1.0 / args.record_fps);
        sim_steps_per_render = std::max(1, static_cast<int>(
            std::round(render_dt / sim_dt)));

        last_time = Window::get_time_sec();

        // Load replay if requested
        if (!args.play_replay_path.empty()) {
            if (!replay_player.load(args.play_replay_path)) {
                PHLOG_ERROR(Engine, "Replay failed to load: " << args.play_replay_path);
            } else {
                PHLOG_INFO(Engine, "Replay loaded: " << args.play_replay_path
                    << " (" << replay_player.events.size() << " events)");
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
            PHLOG_DEBUG(Record, "Record config: output=" << rc.output
                << " fps=" << rc.fps << " preset=" << rc.preset_name
                << " queue_depth=" << rc.queue_depth
                << (rc.audio_path.empty() ? " (no audio)" : " audio=" + rc.audio_path));
            if (!recorder.start(rc, W, H)) {
                PHLOG_ERROR(Record, "Failed to start recording");
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
                        if (sc == SDL_SCANCODE_SPACE) {
                            paused = !paused;
                            PHLOG_DEBUG(Engine, paused ? "Paused" : "Resumed");
                        }
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
            draw_judge_log();
            draw_debug_overlay(frame);
            pause_overlay.draw(ctx.batch, ctx.hud_ren, W, H);
            ctx.window.end_frame();
            remember_debug_frame(frame);
            return !ctx.window.quit_requested;
        }

        // === 3. ADVANCE TIME ===
        if (ctx.has_audio && ctx.started_audio) {
            t = ctx.audio.get_playback_time() + audio_offset_sec;
        } else if (args.headless) {
            t += sim_dt;
        } else {
            t += dt_frame;
        }

        if (ctx.has_audio && !ctx.started_audio && t >= 0.0) {
            ctx.audio.play();
            ctx.started_audio = true;
        }

        // === 4. EXIT CONDITIONS ===
        if (args.duration > 0 && t >= args.duration) {
            if (is_recording) recorder.log_progress(progress_end, progress_end);
            return false;
        }
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
                            ctx.audio.play_hitsound(note.kind);
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
                        if (nidx >= 0 && nidx < static_cast<int>(chart.notes.size()))
                            ctx.audio.play_hitsound(chart.notes[nidx].kind);
                    };
                    auto judge_input = ctx.input.to_judge_input();
                    manual_judge.process_frame(judge_input, mj_frame,
                        chart.notes, states, judge, effects, t, W, H);
                    ctx.input.flush_released();
                }
            }
        } else {
            static std::vector<int> s_hit_notes;
            static std::vector<engine::SimHitEvent> s_hit_events;
            s_hit_notes.clear();
            s_hit_events.clear();
            autoplay.step(t, chart.notes, states, chart.lines, judge, W, H,
                          &s_hit_notes, &s_hit_events);
            // Emit hit effects and hitsounds for each note judged this step
            if (t >= 0.0) {
                for (const auto& ev : s_hit_events) {
                    if (ev.note_idx < 0 || ev.note_idx >= static_cast<int>(chart.notes.size())) continue;
                    const auto& n = chart.notes[ev.note_idx];
                    ctx.audio.play_hitsound(n.kind);
                    if (cfg.show_hitfx) {
                        auto col = resolve_hitfx_color(n, ev.grade);
                        effects.add_hitfx(ev.x, ev.y, ev.judge_t, col);
                        if (cfg.show_particles)
                            effects.add_particle_burst(ev.x, ev.y, ev.judge_t * 1000.0,
                                ctx.respack.cfg.hitfx_duration * 1000.0, col);
                    }
                    push_judge_log(ev.note_idx);
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
                render_scene_at(t_sub, sub_fr, cfg.hitfx_effect_apply);
                prof.record(1, t0_scene, prof.now());
                ctx.motion_blur.add_subframe(ctx.window.ren, ctx.motion_blur.sample_weight(i));
            }
            ctx.motion_blur.composite(ctx.window.ren);
            if (!cfg.hitfx_effect_apply) draw_hitfx_only(t);
        } else if (ctx.trail.enabled()) {
            uint64_t t0_scene = prof.now();
            int trail_samples = args.headless ? std::max(1, sim_steps_per_render) : 1;
            double dt_chart = render_dt * cfg.chart_speed;
            for (int i = 0; i < trail_samples; ++i) {
                double frac = (trail_samples <= 1)
                    ? 1.0
                    : static_cast<double>(i + 1) / static_cast<double>(trail_samples);
                double t_sub = t - dt_chart * (1.0 - frac);
                ctx.trail.begin_frame(ctx.window.ren);
                auto sub_fr = (i == trail_samples - 1)
                    ? frame
                    : render::build_frame(t_sub, chart, states, judge, cfg);
                render_scene_at(t_sub, sub_fr, cfg.hitfx_effect_apply);
                render::RenderTarget::unbind(ctx.window.ren);
                ctx.trail.submit_frame();
            }
            prof.record(1, t0_scene, prof.now());
            ctx.bg.draw(ctx.batch, cfg.bg_dim);
            ctx.trail.composite(ctx.window.ren);
            if (!cfg.hitfx_effect_apply) draw_hitfx_only(t);
        } else {
            ctx.bg.draw(ctx.batch, cfg.bg_dim);
            uint64_t t0_scene = prof.now();
            render_scene_at(t, frame);
            prof.record(1, t0_scene, prof.now());
        }
        uint64_t t1_trail = prof.now();

        ctx.hud_ren.draw(ctx.batch, frame.hud, fps_display);
        draw_debug_overlay(frame);

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
        remember_debug_frame(frame);

        return !ctx.window.quit_requested;
    }

    // ── Public accessors for post-loop reporting ──────────────────────────────
    engine::ScoreResult final_score() const {
        return engine::compute_score(judge.acc_sum, judge.max_combo, playable_notes);
    }

    void finish() {
        if (is_recording) {
            PHLOG_DEBUG(Record, "Finishing recording…");
            recorder.finish();
        }
        if (!args.save_replay_path.empty() && !replay_writer.events.empty()) {
            uint32_t hash = io::chart_path_hash(args.chart_path);
            if (replay_writer.save(args.save_replay_path, hash))
                PHLOG_INFO(Engine, "Replay saved: " << args.save_replay_path
                    << " (" << replay_writer.events.size() << " events)");
            else
                PHLOG_ERROR(Engine, "Replay save failed: " << args.save_replay_path);
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
        PHLOG_DEBUG(Engine, "Chart complete — showing result screen");
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

    bool has_debug(DebugFlag flag) const {
        return has_flag(args.debug_flags, flag);
    }

    void debug_text(double x, double y, const std::string& text,
                    uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                    uint8_t a = 220) const {
        if (!ctx.hud_ren.has_font) return;
        ctx.hud_ren.draw_text(ctx.batch, ctx.hud_ren.font_small, text, x, y, r, g, b, a);
    }

    static const char* note_kind_name(int kind) {
        switch (kind) {
        case 1: return "TAP";
        case 2: return "DRAG";
        case 3: return "HOLD";
        case 4: return "FLICK";
        default: return "?";
        }
    }

    void draw_debug_overlay(const render::FrameSnapshot& fr) {
        if (args.debug_flags == DebugFlag::NONE) return;

        const bool color_map = has_debug(DebugFlag::LINE_INFO_COLOR_MAPPING);
        const double frame_ms = std::max(0.0, dt_frame * 1000.0);
        const double base_note_w = 0.06 * W * cfg.note_scale_x;
        const double base_note_h = 0.018 * H * cfg.note_scale_y;

        // Panel-to-text inset (consistent across all panels)
        constexpr double kPanelPad = 6.0;
        // Row height for stacked debug text (matches font_small size)
        const double kRow = std::max(18.0, ctx.hud_ren.text_line_height(ctx.hud_ren.font_small));
        // Vertical position trackers — start below the HUD stats panel
        double left_y  = std::max(88.0, H * 0.08);
        double right_y = std::max(16.0, H * 0.015);

        auto note_lookup = [&](int nid) -> const Note* {
            auto it = debug_note_by_id.find(nid);
            return it == debug_note_by_id.end() ? nullptr : it->second;
        };

        auto draw_panel = [&](double x, double y, double w, double h) {
            ctx.batch.draw_rect(x, y, w, h, 0, 0, 0, 140);
        };

        auto draw_box_outline = [&](double x, double y, double w, double h,
                                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            ctx.batch.draw_line(x, y, x + w, y, 1.0, r, g, b, a);
            ctx.batch.draw_line(x, y + h, x + w, y + h, 1.0, r, g, b, a);
            ctx.batch.draw_line(x, y, x, y + h, 1.0, r, g, b, a);
            ctx.batch.draw_line(x + w, y, x + w, y + h, 1.0, r, g, b, a);
        };

        // Clamp a right-side panel so its left edge stays on-screen
        auto right_panel_x = [&](double panel_w) -> double {
            return std::max(0.0, static_cast<double>(W) - panel_w - 10.0);
        };

        if (has_debug(DebugFlag::FRAME_TIME)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "frame=%.2fms  fps=%.1f  t=%.3fs  res=%dx%d",
                          frame_ms, fps_display, fr.t, W, H);
            double tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, buf);
            double pw = tw + kPanelPad * 2.0 + 4.0;
            draw_panel(10, left_y - 4, pw, kRow + 8.0);
            debug_text(10 + kPanelPad, left_y, buf, 255, 235, 160, 235);
            left_y += kRow + 10.0;
        }

        if (has_debug(DebugFlag::TIMING_WINDOWS)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "windows: P=%.1fms  G=%.1fms  B=%.1fms",
                          engine::Judge::PERFECT * 1000.0,
                          engine::Judge::GOOD * 1000.0,
                          engine::Judge::BAD * 1000.0);
            double tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, buf);
            double pw = tw + kPanelPad * 2.0 + 4.0;
            draw_panel(10, left_y - 4, pw, kRow + 8.0);
            debug_text(10 + kPanelPad, left_y, buf, 190, 240, 255, 235);
            left_y += kRow + 10.0;
        }

        if (has_debug(DebugFlag::AUDIO_INFO)) {
            int hs_loaded = 0;
            for (int k = 1; k <= 4; ++k)
                if (ctx.audio.hitsounds[k].loaded) ++hs_loaded;
            char line1[160], line2[160];
            std::snprintf(line1, sizeof(line1), "audio: bgm=%s playing=%s hs=%d/4",
                          ctx.audio.bgm_loaded ? "yes" : "no",
                          ctx.audio.is_playing() ? "yes" : "no",
                          hs_loaded);
            std::snprintf(line2, sizeof(line2), "cursor=%.3fs  started=%s  active_ptr=%d",
                          ctx.audio.get_playback_time(),
                          ctx.started_audio ? "yes" : "no",
                          ctx.input.active_count);
            double tw1 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, line1);
            double tw2 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, line2);
            double tw = std::max(tw1, tw2);
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            double panel_h = kRow * 2.0 + 8.0;
            draw_panel(px, right_y - 4, pw, panel_h);
            debug_text(px + kPanelPad, right_y, line1, 180, 255, 200, 235);
            debug_text(px + kPanelPad, right_y + kRow, line2, 180, 220, 255, 225);
            right_y += panel_h + 4.0;
        }

        if (has_debug(DebugFlag::MIRROR_STATUS)) {
            std::string label = std::string("mirror: ") + (mirror_mod_active ? "ON" : "OFF");
            double tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, label);
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            draw_panel(px, right_y - 4, pw, kRow + 8.0);
            debug_text(px + kPanelPad, right_y, label,
                       mirror_mod_active ? 255 : 200,
                       mirror_mod_active ? 180 : 220,
                       180, 235);
            right_y += kRow + 10.0;
        }

        if (has_debug(DebugFlag::PERFORMANCE_PROFILER)) {
            int prof_frames = std::max(1, prof.report_frame);
            double panel_h = kRow * (FramePhaseStats::PHASES + 1) + 8.0;
            // Measure widest profiler line for adaptive width
            double max_tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, "profiler");
            for (int p = 0; p < FramePhaseStats::PHASES; ++p) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%s avg=%.2fms max=%.2fms",
                              FramePhaseStats::phase_name(p),
                              prof.sum_ms[p] / prof_frames,
                              prof.max_ms[p]);
                max_tw = std::max(max_tw, ctx.hud_ren.text_width(ctx.hud_ren.font_small, buf));
            }
            double pw = max_tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            draw_panel(px, right_y - 4, pw, panel_h);
            debug_text(px + kPanelPad, right_y, "profiler", 255, 230, 160, 235);
            for (int p = 0; p < FramePhaseStats::PHASES; ++p) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%s avg=%.2fms max=%.2fms",
                              FramePhaseStats::phase_name(p),
                              prof.sum_ms[p] / prof_frames,
                              prof.max_ms[p]);
                debug_text(px + kPanelPad, right_y + kRow * (p + 1), buf, 220, 220, 220, 220);
            }
            right_y += panel_h + 4.0;
        }

        if (has_debug(DebugFlag::AUDIO_WAVEFORM)) {
            const char* msg = "audio waveform unavailable: PCM taps not exposed";
            double tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, msg);
            double pw = tw + kPanelPad * 2.0 + 4.0;
            draw_panel(10, left_y - 4, pw, kRow + 8.0);
            debug_text(10 + kPanelPad, left_y, msg, 255, 170, 170, 225);
            left_y += kRow + 10.0;
        }

        if (has_debug(DebugFlag::AUDIO_SPECTRUM)) {
            const char* msg = "audio spectrum unavailable: PCM taps not exposed";
            double tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, msg);
            double pw = tw + kPanelPad * 2.0 + 4.0;
            draw_panel(10, left_y - 4, pw, kRow + 8.0);
            debug_text(10 + kPanelPad, left_y, msg, 255, 170, 170, 225);
            left_y += kRow + 10.0;
        }

        if (has_debug(DebugFlag::FRAME_TIME_GRAPH) && !debug_frame_ms_hist.empty()) {
            const double gw = 180.0, gh = 72.0;
            const double gx = 12.0;
            const double gy = std::max(left_y + 4.0, static_cast<double>(H) - gh - 24.0);
            draw_panel(gx, gy, gw, gh);
            double max_ms = 1.0;
            for (double v : debug_frame_ms_hist) max_ms = std::max(max_ms, v);
            double px = gx + 8.0;
            double py = gy + gh - 8.0;
            double dx = (gw - 16.0) / std::max<size_t>(1, debug_frame_ms_hist.size() - 1);
            bool first = true;
            double last_x = 0.0, last_y = 0.0;
            size_t idx = 0;
            for (double v : debug_frame_ms_hist) {
                double x = px + dx * static_cast<double>(idx++);
                double y = py - (gh - 16.0) * (v / max_ms);
                if (!first)
                    ctx.batch.draw_line(last_x, last_y, x, y, 1.0, 120, 255, 180, 220);
                first = false;
                last_x = x;
                last_y = y;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "frame graph (max %.1fms)", max_ms);
            debug_text(gx + 8.0, gy + 4.0, buf, 200, 255, 220, 220);
        }

        if (has_debug(DebugFlag::JUDGE_LINE_INFO_WINDOW)) {
            const double panel_h = std::min<double>(
                std::max(0.0, H - left_y - 12.0),
                kRow * (fr.lines.size() + 1) + 10.0);
            // Measure widest line entry for adaptive panel width
            double max_tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, "judge lines");
            for (const auto& ls : fr.lines) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                              "L%d xy=(%.0f,%.0f) rot=%.1f° a=%.2f s=%.2f sx=%.1f",
                              ls.lid, ls.x, ls.y, ls.rot * 180.0 / M_PI,
                              ls.alpha01, ls.scroll, ls.scale_x);
                max_tw = std::max(max_tw, ctx.hud_ren.text_width(ctx.hud_ren.font_small, buf));
            }
            double pw = max_tw + kPanelPad * 2.0 + 4.0;
            draw_panel(10, left_y - 4, pw, panel_h);
            debug_text(10 + kPanelPad, left_y, "judge lines", 255, 230, 160, 235);
            int row = 0;
            for (const auto& ls : fr.lines) {
                if (left_y + kRow * (row + 1) > H - 20.0) break;
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                              "L%d xy=(%.0f,%.0f) rot=%.1f° a=%.2f s=%.2f sx=%.1f",
                              ls.lid, ls.x, ls.y, ls.rot * 180.0 / M_PI,
                              ls.alpha01, ls.scroll, ls.scale_x);
                uint8_t r = color_map ? ls.color.r : 220;
                uint8_t g = color_map ? ls.color.g : 220;
                uint8_t b = color_map ? ls.color.b : 220;
                debug_text(10 + kPanelPad, left_y + kRow * (++row), buf, r, g, b, 220);
            }
        }

        for (const auto& ls : fr.lines) {
            double cx = ls.x, cy = ls.y;
            render::apply_expand_xy(cx, cy, W, H, cfg.expand_factor);
            uint8_t lr = color_map ? ls.color.r : 255;
            uint8_t lg = color_map ? ls.color.g : 255;
            uint8_t lb = color_map ? ls.color.b : 255;

            if (has_debug(DebugFlag::LINE_GEOMETRY)) {
                double half_len = W * std::max(1.0, cfg.expand_factor) * 0.5 * ls.scale_x;
                double dx = std::cos(ls.rot) * half_len;
                double dy = std::sin(ls.rot) * half_len;
                ctx.batch.draw_line(cx - dx, cy - dy, cx + dx, cy + dy, 1.0, 255, 120, 120, 220);
                ctx.batch.draw_rect(cx - dx - 2.0, cy - dy - 2.0, 4.0, 4.0, 255, 120, 120, 240);
                ctx.batch.draw_rect(cx + dx - 2.0, cy + dy - 2.0, 4.0, 4.0, 120, 255, 180, 240);
            }

            if (has_debug(DebugFlag::JUDGE_LINE_NUMBER)) {
                debug_text(cx + 4.0, cy - 8.0, std::to_string(ls.lid), lr, lg, lb, 240);
            }

            if (has_debug(DebugFlag::JUDGE_LINE_INFO_ABOVE_LINE)) {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "L%d a=%.2f s=%.2f r=%.1f° sx=%.1f sy=%.1f",
                              ls.lid, ls.alpha01, ls.scroll, ls.rot * 180.0 / M_PI,
                              ls.scale_x, ls.scale_y);
                debug_text(cx + 10.0, cy - 22.0, buf, lr, lg, lb, 220);
            }

            if (has_debug(DebugFlag::SPEED_VISUALIZATION)) {
                auto it_pos = debug_prev_line_pos.find(ls.lid);
                auto it_scroll = debug_prev_line_scroll.find(ls.lid);
                if (it_pos != debug_prev_line_pos.end()) {
                    double dx = cx - it_pos->second.first;
                    double dy = cy - it_pos->second.second;
                    ctx.batch.draw_line(cx, cy, cx + dx * 5.0, cy + dy * 5.0, 1.0, 255, 220, 120, 220);
                    if (it_scroll != debug_prev_line_scroll.end()) {
                        char buf[96];
                        std::snprintf(buf, sizeof(buf), "dv=(%.1f,%.1f) ds=%.2f",
                                      dx / std::max(1e-3, dt_frame),
                                      dy / std::max(1e-3, dt_frame),
                                      ls.scroll - it_scroll->second);
                        debug_text(cx + 10.0, cy + 8.0, buf, 255, 220, 120, 220);
                    }
                }
            }
        }

        for (const auto& ns : fr.notes) {
            double x = ns.wx, y = ns.wy;
            render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
            const Note* note = note_lookup(ns.nid);

            if (has_debug(DebugFlag::NOTE_TRAIL)) {
                auto it = debug_note_trails.find(ns.nid);
                if (it != debug_note_trails.end()) {
                    const auto& trail = it->second;
                    for (size_t i = 1; i < trail.size(); ++i) {
                        uint8_t a = static_cast<uint8_t>(60 + 160 * i / std::max<size_t>(1, trail.size() - 1));
                        ctx.batch.draw_line(trail[i - 1].first, trail[i - 1].second,
                                            trail[i].first, trail[i].second,
                                            1.0, 160, 220, 255, a);
                    }
                }
            }

            if (has_debug(DebugFlag::VELOCITY_VECTORS)) {
                auto it = debug_prev_note_pos.find(ns.nid);
                if (it != debug_prev_note_pos.end()) {
                    double dx = x - it->second.first;
                    double dy = y - it->second.second;
                    ctx.batch.draw_line(x, y, x + dx * 4.0, y + dy * 4.0, 1.0, 255, 200, 120, 220);
                }
            }

            if (has_debug(DebugFlag::COMBO_ZONES) && note != nullptr) {
                double until_hit = note->t_hit - fr.t;
                if (until_hit >= 0.0 && until_hit <= engine::Judge::BAD) {
                    uint8_t r = 255, g = 180, b = 120;
                    if (until_hit <= engine::Judge::PERFECT) { r = 255; g = 255; b = 180; }
                    else if (until_hit <= engine::Judge::GOOD) { r = 180; g = 255; b = 180; }
                    double hw = base_note_w * ns.size_px * 0.7;
                    double hh = base_note_h * ns.size_px * 1.2;
                    draw_box_outline(x - hw, y - hh, hw * 2.0, hh * 2.0, r, g, b, 220);
                }
            }

            if (has_debug(DebugFlag::NOTE_HITBOX)) {
                double hw = base_note_w * ns.size_px * 0.5;
                double hh = base_note_h * ns.size_px * 0.5;
                draw_box_outline(x - hw, y - hh, hw * 2.0, hh * 2.0, 120, 220, 255, 220);
                if (ns.is_hold) {
                    double tx = ns.wx_tail, ty = ns.wy_tail;
                    render::apply_expand_xy(tx, ty, W, H, cfg.expand_factor);
                    ctx.batch.draw_line(x, y, tx, ty, 1.0, 120, 220, 255, 180);
                }
            }

            if (has_debug(DebugFlag::NOTE_LINE_NUMBER)) {
                debug_text(x + 8.0, y + 8.0, std::to_string(ns.nid), 255, 255, 160, 235);
            }

            if (has_debug(DebugFlag::NOTE_JUDGE_WINDOW) && note != nullptr) {
                double dt_ms = (note->t_hit - fr.t) * 1000.0;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "dt=%+.1fms k=%s", dt_ms,
                              note_kind_name(note->kind));
                uint8_t r = 255, g = 220, b = 160;
                double adt = std::abs(dt_ms);
                if (adt <= engine::Judge::PERFECT * 1000.0) { r = 255; g = 255; b = 180; }
                else if (adt <= engine::Judge::GOOD * 1000.0) { r = 180; g = 255; b = 180; }
                else if (adt <= engine::Judge::BAD * 1000.0) { r = 255; g = 210; b = 150; }
                else { r = 255; g = 150; b = 150; }
                debug_text(x + 10.0, y - 16.0, buf, r, g, b, 220);
            }

            if (has_debug(DebugFlag::NOTE_INFO) && note != nullptr) {
                double hold_prog = 0.0;
                if (note->kind == 3 && note->t_end > note->t_hit)
                    hold_prog = std::clamp((fr.t - note->t_hit) / (note->t_end - note->t_hit), 0.0, 1.0);
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "N%d L%d %s t=%.3fs xy=(%.0f,%.0f) a=%.2f sz=%.1f hp=%.0f%%",
                              ns.nid, note->line_id, note_kind_name(note->kind),
                              note->t_hit, ns.wx, ns.wy, ns.alpha, ns.size_px,
                              hold_prog * 100.0);
                debug_text(x + 10.0, y + 10.0, buf, 220, 220, 255, 220);
            }
        }

        if (has_debug(DebugFlag::TOUCH_VISUALIZATION)) {
            for (const auto& slot : ctx.input.slots) {
                if (!slot.active()) continue;
                draw_box_outline(slot.x - 12.0, slot.y - 12.0, 24.0, 24.0,
                                 slot.down ? 255 : 200, 220, 120, 235);
                ctx.batch.draw_line(slot.x, slot.y,
                                    slot.x + slot.vx * 0.04, slot.y + slot.vy * 0.04,
                                    1.0, 255, 220, 120, 235);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "id=%" PRId64 " v=%.0f",
                              slot.id, slot.peak_speed);
                debug_text(slot.x + 14.0, slot.y - 8.0, buf, 255, 220, 120, 235);
            }
        }
    }

    void remember_debug_frame(const render::FrameSnapshot& fr) {
        if (args.debug_flags == DebugFlag::NONE) return;

        debug_frame_ms_hist.push_back(std::max(0.0, dt_frame * 1000.0));
        while (debug_frame_ms_hist.size() > 180) debug_frame_ms_hist.pop_front();

        for (const auto& ls : fr.lines) {
            double x = ls.x, y = ls.y;
            render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
            debug_prev_line_pos[ls.lid] = {x, y};
            debug_prev_line_scroll[ls.lid] = ls.scroll;
        }

        for (const auto& ns : fr.notes) {
            double x = ns.wx, y = ns.wy;
            render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
            debug_prev_note_pos[ns.nid] = {x, y};
            auto& trail = debug_note_trails[ns.nid];
            trail.emplace_back(x, y);
            while (trail.size() > 12) trail.pop_front();
        }
    }

    // Record into DrawList and execute via SdlExecutor.
    void render_scene_at(double t_r, const render::FrameSnapshot& fr, bool include_hitfx = true) {
        // Adaptive reserve: reuse last-frame command count to avoid realloc
        static thread_local size_t s_last_dl_sz = 256;
        ctx.draw_list.clear();
        ctx.draw_list.cmds.reserve(s_last_dl_sz + 32);
        ctx.batch.dl = &ctx.draw_list;

        // RPE isCover: non-cover lines drawn first (behind notes),
        // cover lines drawn last (in front of notes).
        ctx.hold_ren.draw(ctx.batch, ctx.respack, fr.notes, t_r, W, H, cfg.expand_factor);
        ctx.line_ren.draw(ctx.batch, ctx.respack.white_tex, fr.lines, W, H, cfg.expand_factor, /*cover_pass=*/false);
        ctx.note_ren.draw(ctx.batch, ctx.respack, fr.notes, t_r, W, H, cfg.expand_factor);
        ctx.line_ren.draw(ctx.batch, ctx.respack.white_tex, fr.lines, W, H, cfg.expand_factor, /*cover_pass=*/true);

        if (include_hitfx) {
            ctx.hitfx_ren.draw(ctx.batch, ctx.respack, effects, t_r,
                               cfg.show_hitfx, cfg.show_particles,
                               static_cast<float>(cfg.hitfx_intensity),
                               W, H, cfg.expand_factor);
        }
        draw_simulateplay_overlay(t_r);
        s_last_dl_sz = ctx.draw_list.cmds.size();
        ctx.batch.dl = nullptr;
        render::SdlExecutor::execute(ctx.window.ren, ctx.draw_list);
    }

    void draw_simulateplay_overlay(double t_r) {
        if (is_play_mode || !cfg.simulateplay.enabled || !autoplay.render_enabled()) return;

        const auto& visuals = autoplay.visuals();
        const double radius = autoplay.cursor_radius_px();
        for (const auto& pointer : visuals) {
            if (autoplay.trail_enabled()) {
                for (size_t i = 0; i < pointer.trail.size(); ++i) {
                    const auto& sample = pointer.trail[i];
                    double age = std::max(0.0, t_r - sample.t);
                    double alpha01 = std::max(0.0, 1.0 - age / std::max(0.02, cfg.simulateplay.trail_seconds));
                    uint8_t a = static_cast<uint8_t>(std::clamp(alpha01 * 120.0, 0.0, 255.0));
                    if (a == 0) continue;
                    double x = sample.x, y = sample.y;
                    render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
                    double sz = std::max(4.0, radius * (0.4 + alpha01 * 0.8));
                    ctx.batch.draw_rect(x - sz * 0.5, y - sz * 0.5, sz, sz,
                                        180, 240, 255, a);
                }
            }

            if (pointer.fade_alpha <= 0.001 && pointer.trail.empty()) continue;
            double x = pointer.x, y = pointer.y;
            render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
            double fade = std::clamp(pointer.fade_alpha, 0.0, 1.0);
            double swell = 1.0 + 0.18 * pointer.fade_progress;
            uint8_t inner_a = static_cast<uint8_t>((pointer.down ? 220.0 : 150.0) * fade);
            uint8_t outer_a = static_cast<uint8_t>((pointer.flick ? 255.0 : 190.0) * fade);
            double inner = (pointer.down ? radius * 0.75 : radius * 0.60) * swell;
            double outer = radius * swell;
            ctx.batch.draw_rect(x - inner * 0.5, y - inner * 0.5, inner, inner,
                                235, 248, 255, inner_a);
            ctx.batch.draw_rect(x - outer * 0.5, y - outer * 0.5, outer, outer,
                                80, 220, 255, outer_a);
        }
    }

    void draw_hitfx_only(double t_r) {
        if (!cfg.show_hitfx && !cfg.show_particles) return;
        ctx.draw_list.clear();
        ctx.batch.dl = &ctx.draw_list;
        ctx.hitfx_ren.draw(ctx.batch, ctx.respack, effects, t_r,
                           cfg.show_hitfx, cfg.show_particles,
                           static_cast<float>(cfg.hitfx_intensity),
                           W, H, cfg.expand_factor);
        ctx.batch.dl = nullptr;
        render::SdlExecutor::execute(ctx.window.ren, ctx.draw_list);
    }

    void push_judge_log(int note_idx) {
        if (note_idx < 0 || note_idx >= static_cast<int>(states.size())) return;
        const auto& s = states[note_idx];
        if (s.judge_grade.empty()) return;

        std::ostringstream oss;
        oss << "note " << note_idx
            << " " << s.judge_grade
            << " " << std::showpos << std::fixed << std::setprecision(1)
            << s.judge_delta_ms << "ms";

        if (s.note && s.note->kind == 3) {
            double held_ms = 0.0;
            if (s.released_early) held_ms = std::max(0.0, (s.release_t - s.note->t_hit) * 1000.0);
            else if (s.hold_finalized) held_ms = std::max(0.0, (s.note->t_end - s.note->t_hit) * 1000.0);
            else held_ms = std::max(0.0, (s.judge_t - s.note->t_hit) * 1000.0);
            oss << " " << std::noshowpos << std::fixed << std::setprecision(1)
                << held_ms << "ms";
        }

        recent_judges.push_front(oss.str());
        while (recent_judges.size() > 8) recent_judges.pop_back();
    }

    void draw_judge_log() {
        if (!cfg.simulateplay.enabled || !ctx.hud_ren.has_font || recent_judges.empty()) return;
        const double x = 16.0;
        const double y0 = H - 28.0 - 8.0 * 22.0;
        int row = 0;
        for (const auto& line : recent_judges) {
            uint8_t a = static_cast<uint8_t>(std::max(120, 240 - row * 14));
            ctx.hud_ren.draw_text(ctx.batch, ctx.hud_ren.font_small, line, x, y0 + row * 22.0,
                                  220, 235, 255, a);
            ++row;
        }
    }

    void do_restart() {
        PHLOG_INFO(Engine, "Restarting chart");
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
        autoplay.reset();
        autoplay.set_humanize(cfg.simulateplay.enabled, cfg.simulateplay.jitter_ms);
        autoplay.set_visuals(cfg.simulateplay.enabled && cfg.simulateplay.render_pointer,
                             cfg.simulateplay.enabled && cfg.simulateplay.render_trail,
                             cfg.simulateplay.trail_seconds,
                             cfg.simulateplay.cursor_radius_px);
        replay_player.cursor = 0;
        recent_judges.clear();
        ctx.reload_audio(chart.offset);
    }

    static engine::SimMode parse_sim_mode(const std::string& mode) {
        if (mode == "conservative") return engine::SimMode::Conservative;
        if (mode == "extreme") return engine::SimMode::Extreme;
        return engine::SimMode::Aggressive;
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
            PHLOG_ERROR(Record, "Capture failed, stopping recorder");
            recorder.finish();
            is_recording = false;
            return;
        }
        if (++record_log_frames % static_cast<int>(args.record_fps) == 0)
            recorder.log_progress(t, progress_end);
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
