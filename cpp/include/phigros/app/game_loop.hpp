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
    const bool                         mute_live_audio;

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
    bool                    record_profile_reported = false;

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

    // ── Debug state for new overlays ─────────────────────────────────────────
    // MISS_INDICATOR: track recently missed note IDs with a fade timer
    std::unordered_map<int, double> debug_miss_flash;  // nid -> time_remaining (seconds)
    // JUDGMENT_HISTORY: recent judgment results for feed display
    struct JudgeEntry { int nid; std::string text; double age; };
    std::deque<JudgeEntry> debug_judge_history;
    // SCORE_BREAKDOWN: per-grade counters (updated from NoteState)
    int debug_cnt_perfect = 0, debug_cnt_good = 0, debug_cnt_bad = 0, debug_cnt_miss = 0;

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

    struct RecordCliProfiler {
        static constexpr int PHASES = 9;
        static constexpr int REPORT_INTERVAL = 120;
        enum Phase {
            SimUpdate = 0,
            BuildFrame = 1,
            RenderScene = 2,
            HudPresent = 3,
            ReadbackAPI = 4,
            ReadbackConvert = 5,
            ReadbackCopy = 6,
            RecorderQueue = 7,
            FrameTotal = 8,
        };

        uint64_t freq = 1;
        int frames = 0;
        int reports = 0;
        double sum_ms[PHASES] = {};
        double max_ms[PHASES] = {};

        static const char* phase_name(int i) {
            static const char* names[PHASES] = {
                "sim_update",
                "build_frame",
                "render_scene",
                "hud_present",
                "readback_api",
                "readback_convert",
                "readback_copy",
                "record_queue",
                "frame_total"
            };
            return names[i];
        }

        void init() { freq = SDL_GetPerformanceFrequency(); }
        uint64_t now() const { return SDL_GetPerformanceCounter(); }
        double to_ms(uint64_t ticks) const {
            return static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq);
        }
        void record(int phase, uint64_t start, uint64_t end) {
            const double ms = to_ms(end - start);
            record_ms(phase, ms);
        }
        void record_ms(int phase, double ms) {
            sum_ms[phase] += ms;
            max_ms[phase] = std::max(max_ms[phase], ms);
        }
        void finish_frame() { ++frames; }
        void maybe_report(const io::RecordingSession& recorder) {
            if (frames == 0 || frames % REPORT_INTERVAL != 0) return;
            ++reports;
            const int window = REPORT_INTERVAL;
            double worst_mean = -1.0;
            int worst_phase = 0;
            std::ostringstream oss;
            oss << "[RecordProfile] window=" << reports
                << " frames=" << window;
            for (int p = 0; p < PHASES; ++p) {
                const double mean = sum_ms[p] / window;
                if (p != FrameTotal && mean > worst_mean) {
                    worst_mean = mean;
                    worst_phase = p;
                }
                oss << " | " << phase_name(p)
                    << " avg=" << std::fixed << std::setprecision(2) << mean
                    << "ms max=" << max_ms[p] << "ms";
            }
            auto enc = recorder.stats_snapshot();
            auto rec = recorder.profiler_stats_snapshot();
            oss << " | bottleneck=" << phase_name(worst_phase)
                << " | encode_write avg=" << enc.avg_write_ms()
                << "ms max=" << enc.max_write_ms
                << "ms"
                << " | queue_wait avg=" << rec.avg_enqueue_wait_ms()
                << "ms";
            PHLOG_INFO(Record, oss.str());
            reset_window();
        }
        void final_report(const io::RecordingSession& recorder) {
            if (frames == 0) return;
            merge_window_into_totals();
            double worst_mean = -1.0;
            int worst_phase = 0;
            std::ostringstream oss;
            oss << "[RecordProfile] summary frames=" << frames;
            for (int p = 0; p < PHASES; ++p) {
                const double mean = total_sum_ms[p] / frames;
                if (p != FrameTotal && mean > worst_mean) {
                    worst_mean = mean;
                    worst_phase = p;
                }
                oss << " | " << phase_name(p)
                    << " avg=" << std::fixed << std::setprecision(2) << mean
                    << "ms max=" << total_max_ms[p] << "ms";
            }
            auto enc = recorder.stats_snapshot();
            auto rec = recorder.profiler_stats_snapshot();
            oss << " | bottleneck=" << phase_name(worst_phase)
                << " | encode_write avg=" << enc.avg_write_ms()
                << "ms max=" << enc.max_write_ms
                << "ms slow=" << enc.slow_writes
                << " | queue_wait avg=" << rec.avg_enqueue_wait_ms()
                << "ms max=" << rec.enqueue_max_wait_ms
                << "ms"
                << " | queue_copy avg=" << rec.avg_enqueue_copy_ms()
                << "ms max=" << rec.enqueue_max_copy_ms
                << "ms"
                << " | close=" << rec.finish_encoder_close_ms
                << "ms"
                << " | audio_mix=" << rec.finish_audio_mix_ms
                << "ms"
                << " | mux=" << rec.finish_mux_ms << "ms";
            PHLOG_INFO(Record, oss.str());
        }
        void merge_window_into_totals() {
            for (int p = 0; p < PHASES; ++p) {
                total_sum_ms[p] += sum_ms[p];
                total_max_ms[p] = std::max(total_max_ms[p], max_ms[p]);
            }
        }
    private:
        double total_sum_ms[PHASES] = {};
        double total_max_ms[PHASES] = {};
        void reset_window() {
            merge_window_into_totals();
            for (int p = 0; p < PHASES; ++p) {
                sum_ms[p] = 0.0;
                max_ms[p] = 0.0;
            }
        }
    } record_prof;

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
        , mute_live_audio(!args_.record_output.empty())
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
        const double effective_sim_fps = (args.sim_fps > 0.0) ? args.sim_fps : args.record_fps;
        sim_dt = 1.0 / std::max(1.0, effective_sim_fps);
        render_dt = args.record_output.empty() ? (1.0 / 60.0)
                                               : (1.0 / args.record_fps);
        sim_steps_per_render = std::max(1, static_cast<int>(
            std::round(render_dt / sim_dt)));
        PHLOG_INFO(Engine, "GameLoop init: mode="
            << (is_play_mode ? (replay_player.enabled() ? "replay" : "play") : "autoplay")
            << " notes=" << chart.notes.size()
            << " playable=" << playable_notes
            << " chart_end=" << chart_end
            << " progress_end=" << progress_end);
        PHLOG_DEBUG(Engine, "GameLoop timing: sim_dt=" << sim_dt
            << " render_dt=" << render_dt
            << " sim_fps=" << effective_sim_fps
            << " sim_steps_per_render=" << sim_steps_per_render
            << " audio_offset_sec=" << audio_offset_sec);

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
            rc.chart_offset = chart.offset;
            rc.audio_offset_sec = audio_offset_sec;
            if (rc.audio_path.empty())
                rc.audio_path = find_chart_audio(
                    std::filesystem::path(args.chart_path).parent_path().string());
            for (int k = 1; k <= 4; ++k)
                rc.hitsound_ogg[k] = ctx.respack.hitsound_ogg[k];
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
        record_prof.init();
    }

    // ── run_frame() ──────────────────────────────────────────────────────────
    // Returns true to keep running, false to exit the loop.
    bool run_frame() {
        uint64_t rp_frame_begin = 0;
        if (args.record_profile && is_recording)
            rp_frame_begin = record_prof.now();
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
        if (!mute_live_audio && ctx.has_audio && ctx.started_audio) {
            t = ctx.audio.get_playback_time() + audio_offset_sec;
        } else if (args.headless) {
            t += sim_dt;
        } else {
            t += dt_frame;
        }

        if (!mute_live_audio && ctx.has_audio && !ctx.started_audio && t >= 0.0) {
            PHLOG_DEBUG(Audio, "Audio start threshold reached at t=" << t);
            ctx.audio.play();
            ctx.started_audio = true;
        }

        // === 4. EXIT CONDITIONS ===
        if (args.duration > 0 && t >= args.duration) {
            PHLOG_INFO(Engine, "Stopping at duration limit: t=" << t
                << " limit=" << args.duration);
            if (is_recording) recorder.log_progress(progress_end, progress_end);
            return false;
        }
        if (t > chart_end) {
            PHLOG_INFO(Engine, "Stopping at chart end: t=" << t << " chart_end=" << chart_end);
            if (is_play_mode && !result_shown) mark_result();
            if (!is_play_mode) return false;
        }
        if (result_shown && Window::get_time_sec() - result_t > 5.0) return false;
        if (!mute_live_audio && ctx.has_audio && ctx.started_audio && ctx.audio.is_at_end() && t > 1.0) {
            PHLOG_INFO(Engine, "Stopping because audio reached end at t=" << t);
            if (is_play_mode && !result_shown) mark_result();
            if (!is_play_mode) return false;
        }

        // === 5. ENGINE UPDATE ===
        uint64_t rp_sim_begin = 0;
        if (args.record_profile && is_recording)
            rp_sim_begin = record_prof.now();
        if (is_play_mode) {
            if (!result_shown && t >= 0.0) {
                if (replay_player.enabled()) {
                    replay_player.tick(t, chart.notes, states, judge,
                        [&](int nidx, float ft, const std::string& g) {
                            if (nidx < 0 || nidx >= static_cast<int>(chart.notes.size())) return;
                            const auto& note = chart.notes[nidx];
                            if (!mute_live_audio) ctx.audio.play_hitsound(note.kind);
                            if (is_recording) recorder.record_hitsound(note.kind, ft);
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
                        if (nidx >= 0 && nidx < static_cast<int>(chart.notes.size())) {
                            int kind = chart.notes[nidx].kind;
                            if (!mute_live_audio) ctx.audio.play_hitsound(kind);
                            if (is_recording) recorder.record_hitsound(kind, ft);
                        }
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
                    if (!mute_live_audio) ctx.audio.play_hitsound(n.kind);
                    if (is_recording) recorder.record_hitsound(n.kind, ev.judge_t);
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
        if (args.record_profile && is_recording)
            record_prof.record(RecordCliProfiler::SimUpdate, rp_sim_begin, record_prof.now());

        // === 6. SKIP RENDER ON INTERMEDIATE SIM TICKS ===
        if (args.headless && ++headless_sub < sim_steps_per_render) return true;
        headless_sub = 0;

        // === 7. BUILD FRAME SNAPSHOT ===
        uint64_t t0_build = prof.now();
        auto frame = render::build_frame(t, chart, states, judge, cfg);
        uint64_t t1_build = prof.now();
        if (args.record_profile && is_recording)
            record_prof.record(RecordCliProfiler::BuildFrame, t0_build, t1_build);

        // === 8. RENDER ===
        ctx.window.begin_frame();

        uint64_t t0_trail = prof.now();
        uint64_t rp_render_begin = (args.record_profile && is_recording) ? record_prof.now() : 0;
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
        if (args.record_profile && is_recording)
            record_prof.record(RecordCliProfiler::RenderScene, rp_render_begin, record_prof.now());

        uint64_t rp_hud_begin = (args.record_profile && is_recording) ? record_prof.now() : 0;
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
        if (args.record_profile && is_recording)
            record_prof.record(RecordCliProfiler::HudPresent, rp_hud_begin, record_prof.now());

        // === 9. VIDEO CAPTURE ===
        uint64_t t0_readback = prof.now();
        uint64_t rp_capture_begin = (args.record_profile && is_recording) ? record_prof.now() : 0;
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
        if (args.record_profile && (is_recording || rp_capture_begin != 0)) {
            if (last_capture_readback_api_ms_ > 0.0)
                record_prof.record_ms(RecordCliProfiler::ReadbackAPI,
                                      last_capture_readback_api_ms_);
            if (last_capture_readback_convert_ms_ > 0.0)
                record_prof.record_ms(RecordCliProfiler::ReadbackConvert,
                                      last_capture_readback_convert_ms_);
            if (last_capture_readback_copy_ms_ > 0.0)
                record_prof.record_ms(RecordCliProfiler::ReadbackCopy,
                                      last_capture_readback_copy_ms_);
            if (last_capture_queue_ms_ > 0.0)
                record_prof.record_ms(RecordCliProfiler::RecorderQueue,
                                      last_capture_queue_ms_);
            if (rp_frame_begin != 0)
                record_prof.record(RecordCliProfiler::FrameTotal, rp_frame_begin, record_prof.now());
            record_prof.finish_frame();
            record_prof.maybe_report(recorder);
        }
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
            stop_recording_session();
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
        auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, playable_notes);
        PHLOG_INFO(Engine, "Chart complete: score=" << sr.score
            << " acc=" << sr.acc_ratio
            << " combo=" << judge.max_combo << "/" << playable_notes);
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

    bool note_uses_mh_texture(const render::NoteSnapshot& ns) const {
        if (!ns.mh) return false;
        switch (ns.kind) {
        case 1: return ctx.respack.click_mh.valid();
        case 2: return ctx.respack.drag_mh.valid();
        case 3: return ctx.respack.has_hold_mh_variant();
        case 4: return ctx.respack.flick_mh.valid();
        default: return false;
        }
    }

    void draw_debug_overlay(const render::FrameSnapshot& fr) {
        if (args.debug_flags == DebugFlag::NONE) return;

        const bool color_map = has_debug(DebugFlag::LINE_INFO_COLOR_MAPPING);
        const double frame_ms = std::max(0.0, dt_frame * 1000.0);
        const double base_note_w = 0.06 * W * cfg.note_scale_x;
        const double base_note_h = 0.018 * H * cfg.note_scale_y;

        // Panel-to-text inset (consistent across all panels)
        const double kPanelPad = 6.0 * cfg.font_size;
        const uint8_t kPanelAlpha = static_cast<uint8_t>(cfg.overlay_transparent ? 88 : 140);
        // Row height for stacked debug text (matches font_small size)
        const double kRow = std::max(18.0 * cfg.font_size, ctx.hud_ren.text_line_height(ctx.hud_ren.font_small));
        // Vertical position trackers — start below the HUD stats panel
        double left_y  = std::max(96.0 * cfg.font_size, H * 0.08);
        double right_y = std::max(16.0 * cfg.font_size, H * 0.015);

        auto note_lookup = [&](int nid) -> const Note* {
            auto it = debug_note_by_id.find(nid);
            return it == debug_note_by_id.end() ? nullptr : it->second;
        };

        const double audio_t = mute_live_audio
            ? (fr.t - audio_offset_sec)
            : ctx.audio.get_playback_time();
        const double audio_drift_ms = ((audio_t + audio_offset_sec) - fr.t) * 1000.0;

        int visible_by_kind[5] = {};
        int pending_notes = 0;
        int judged_notes = 0;
        int missed_notes = 0;
        int active_hold_notes = 0;
        int holding_notes = 0;
        for (const auto& st : states) {
            if (st.miss) ++missed_notes;
            else if (st.judged || st.hold_finalized) ++judged_notes;
            else ++pending_notes;
            if (st.note && st.note->kind == 3 && !st.hold_finalized) {
                ++active_hold_notes;
                if (st.holding) ++holding_notes;
            }
        }

        struct LineActivityRow {
            int lid = 0;
            int visible = 0;
            int imminent = 0;
            int holds = 0;
            double alpha01 = 0.0;
            double scroll = 0.0;
        };
        std::vector<LineActivityRow> line_activity;
        line_activity.reserve(fr.lines.size());
        std::unordered_map<int, size_t> line_activity_index;
        for (size_t i = 0; i < fr.lines.size(); ++i) {
            const auto& ls = fr.lines[i];
            line_activity.push_back({ls.lid, 0, 0, 0, ls.alpha01, ls.scroll});
            line_activity_index[ls.lid] = i;
        }

        int visible_multi = 0;
        for (const auto& ns : fr.notes) {
            if (ns.kind >= 1 && ns.kind <= 4) ++visible_by_kind[ns.kind];
            if (ns.mh) ++visible_multi;
            const Note* note = note_lookup(ns.nid);
            if (!note) continue;
            auto it = line_activity_index.find(note->line_id);
            if (it == line_activity_index.end()) continue;
            auto& row = line_activity[it->second];
            ++row.visible;
            if (ns.is_hold) ++row.holds;
            const double until_hit = note->t_hit - fr.t;
            if (until_hit >= 0.0 && until_hit <= 1.0) ++row.imminent;
        }

        auto draw_panel = [&](double x, double y, double w, double h) {
            ctx.batch.draw_rect(x, y, w, h, 0, 0, 0, kPanelAlpha);
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

        if (has_debug(DebugFlag::TIMING_CLOCKS)) {
            char l1[160], l2[160], l3[160];
            std::snprintf(l1, sizeof(l1), "sim=%.3fs  audio=%.3fs  drift=%+.1fms",
                          fr.t, audio_t, audio_drift_ms);
            std::snprintf(l2, sizeof(l2), "render_dt=%.2fms  sim_dt=%.2fms  sim/render=%d",
                          render_dt * 1000.0, sim_dt * 1000.0, sim_steps_per_render);
            std::snprintf(l3, sizeof(l3), "mode=%s  paused=%s  result=%s",
                          is_recording ? "record" : (is_play_mode ? "play" : "autoplay"),
                          paused ? "yes" : "no",
                          result_shown ? "yes" : "no");
            double tw = std::max({ctx.hud_ren.text_width(ctx.hud_ren.font_small, l1),
                                  ctx.hud_ren.text_width(ctx.hud_ren.font_small, l2),
                                  ctx.hud_ren.text_width(ctx.hud_ren.font_small, l3)});
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double panel_h = kRow * 3.0 + 8.0;
            draw_panel(10, left_y - 4, pw, panel_h);
            debug_text(10 + kPanelPad, left_y, l1, 255, 230, 180, 235);
            debug_text(10 + kPanelPad, left_y + kRow, l2, 180, 220, 255, 225);
            debug_text(10 + kPanelPad, left_y + kRow * 2.0, l3, 200, 255, 200, 225);
            left_y += panel_h + 6.0;
        }

        if (has_debug(DebugFlag::VISIBILITY_SUMMARY)) {
            char l1[160], l2[160], l3[160];
            std::snprintf(l1, sizeof(l1), "visible T=%d D=%d H=%d F=%d  multi=%d",
                          visible_by_kind[1], visible_by_kind[2],
                          visible_by_kind[3], visible_by_kind[4], visible_multi);
            std::snprintf(l2, sizeof(l2), "state pending=%d  judged=%d  miss=%d",
                          pending_notes, judged_notes, missed_notes);
            std::snprintf(l3, sizeof(l3), "holds active=%d  holding=%d  frame_notes=%zu",
                          active_hold_notes, holding_notes, fr.notes.size());
            double tw = std::max({ctx.hud_ren.text_width(ctx.hud_ren.font_small, l1),
                                  ctx.hud_ren.text_width(ctx.hud_ren.font_small, l2),
                                  ctx.hud_ren.text_width(ctx.hud_ren.font_small, l3)});
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double panel_h = kRow * 3.0 + 8.0;
            draw_panel(10, left_y - 4, pw, panel_h);
            debug_text(10 + kPanelPad, left_y, l1, 220, 240, 255, 235);
            debug_text(10 + kPanelPad, left_y + kRow, l2, 200, 255, 200, 225);
            debug_text(10 + kPanelPad, left_y + kRow * 2.0, l3, 255, 220, 180, 225);
            left_y += panel_h + 6.0;
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
                          audio_t,
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

        if (has_debug(DebugFlag::RECORDING_STATUS)) {
            char l1[160], l2[160], l3[160];
            if (is_recording && recorder.is_active()) {
                auto s = recorder.stats_snapshot();
                std::snprintf(l1, sizeof(l1), "record %.0ffps wall=%.1f q=%zu/%zu",
                              recorder.target_fps(), s.fps_wall(),
                              recorder.queue_size_snapshot(), recorder.queue_capacity());
                std::snprintf(l2, sizeof(l2), "capture=%dx%d output=%dx%d",
                              recorder.capture_width(), recorder.capture_height(),
                              recorder.output_width(), recorder.output_height());
                std::snprintf(l3, sizeof(l3), "frames=%d avg=%.2fms max=%.2fms",
                              s.frames_written, s.avg_write_ms(), s.max_write_ms);
            } else {
                std::snprintf(l1, sizeof(l1), "record inactive");
                std::snprintf(l2, sizeof(l2), "target=%.0ffps queue=%d",
                              args.record_fps, args.record_queue_depth);
                std::snprintf(l3, sizeof(l3), "capture=%dx%d output=%dx%d",
                              W, H,
                              args.record_w > 0 ? args.record_w : W,
                              args.record_h > 0 ? args.record_h : H);
            }
            double tw = std::max({ctx.hud_ren.text_width(ctx.hud_ren.font_small, l1),
                                  ctx.hud_ren.text_width(ctx.hud_ren.font_small, l2),
                                  ctx.hud_ren.text_width(ctx.hud_ren.font_small, l3)});
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            double panel_h = kRow * 3.0 + 8.0;
            draw_panel(px, right_y - 4, pw, panel_h);
            debug_text(px + kPanelPad, right_y, l1, 255, 230, 180, 235);
            debug_text(px + kPanelPad, right_y + kRow, l2, 180, 220, 255, 225);
            debug_text(px + kPanelPad, right_y + kRow * 2.0, l3, 200, 255, 200, 225);
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

        if (has_debug(DebugFlag::LINE_ACTIVITY_PANEL) && !line_activity.empty()) {
            std::sort(line_activity.begin(), line_activity.end(),
                      [](const LineActivityRow& a, const LineActivityRow& b) {
                          if (a.visible != b.visible) return a.visible > b.visible;
                          if (a.imminent != b.imminent) return a.imminent > b.imminent;
                          return a.lid < b.lid;
                      });
            int rows = std::min<int>(5, static_cast<int>(line_activity.size()));
            double max_tw = ctx.hud_ren.text_width(ctx.hud_ren.font_small, "line activity");
            char buf[160];
            for (int i = 0; i < rows; ++i) {
                const auto& row = line_activity[i];
                std::snprintf(buf, sizeof(buf), "L%d vis=%d hit<1s=%d hold=%d a=%.2f s=%.2f",
                              row.lid, row.visible, row.imminent, row.holds, row.alpha01, row.scroll);
                max_tw = std::max(max_tw, ctx.hud_ren.text_width(ctx.hud_ren.font_small, buf));
            }
            double pw = max_tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            double panel_h = kRow * (rows + 1) + 8.0;
            draw_panel(px, right_y - 4, pw, panel_h);
            debug_text(px + kPanelPad, right_y, "line activity", 255, 230, 160, 235);
            for (int i = 0; i < rows; ++i) {
                const auto& row = line_activity[i];
                std::snprintf(buf, sizeof(buf), "L%d vis=%d hit<1s=%d hold=%d a=%.2f s=%.2f",
                              row.lid, row.visible, row.imminent, row.holds, row.alpha01, row.scroll);
                debug_text(px + kPanelPad, right_y + kRow * (i + 1), buf,
                           220, static_cast<uint8_t>(220 + std::min(35, row.visible * 6)), 255, 220);
            }
            right_y += panel_h + 4.0;
        }

        if (has_debug(DebugFlag::AUDIO_WAVEFORM)) {
            const double gw = 220.0 * cfg.font_size;
            const double gh = 80.0 * cfg.font_size;
            const double gx = 10.0;
            const double gy = left_y - 4.0;
            draw_panel(gx, gy, gw, gh);
            debug_text(gx + kPanelPad, gy + 4.0, "audio waveform", 180, 235, 255, 225);
            if (ctx.audio.has_pcm_tap()) {
                auto pcm = ctx.audio.capture_recent_pcm_at_playback_time(audio_t, 160);
                double mid_y = gy + gh * 0.58;
                double amp = gh * 0.28;
                double px = gx + 8.0;
                double py = mid_y;
                double dx = (gw - 16.0) / std::max<size_t>(1, pcm.size() - 1);
                bool first = true;
                for (size_t i = 0; i < pcm.size(); ++i) {
                    double x = gx + 8.0 + dx * static_cast<double>(i);
                    double y = mid_y - amp * std::clamp<double>(pcm[i], -1.0, 1.0);
                    if (!first)
                        ctx.batch.draw_line(px, py, x, y, 1.0, 120, 255, 200, 220);
                    first = false;
                    px = x;
                    py = y;
                }
                ctx.batch.draw_line(gx + 8.0, mid_y, gx + gw - 8.0, mid_y, 1.0, 100, 120, 130, 120);
            } else {
                debug_text(gx + kPanelPad, gy + kRow + 8.0, "PCM tap unavailable", 255, 170, 170, 225);
            }
            left_y += gh + 8.0;
        }

        if (has_debug(DebugFlag::AUDIO_SPECTRUM)) {
            const double gw = 220.0 * cfg.font_size;
            const double gh = 88.0 * cfg.font_size;
            const double gx = 10.0;
            const double gy = left_y - 4.0;
            draw_panel(gx, gy, gw, gh);
            debug_text(gx + kPanelPad, gy + 4.0, "audio spectrum", 180, 235, 255, 225);
            if (ctx.audio.has_pcm_tap()) {
                auto pcm = ctx.audio.capture_recent_pcm_at_playback_time(audio_t, 256);
                constexpr int BINS = 24;
                double mags[BINS] = {};
                for (int k = 0; k < BINS; ++k) {
                    double re = 0.0, im = 0.0;
                    for (size_t n = 0; n < pcm.size(); ++n) {
                        double phase = 2.0 * M_PI * static_cast<double>(k + 1) * static_cast<double>(n)
                                     / static_cast<double>(pcm.size());
                        re += pcm[n] * std::cos(phase);
                        im -= pcm[n] * std::sin(phase);
                    }
                    mags[k] = std::sqrt(re * re + im * im) / std::max<size_t>(1, pcm.size());
                }
                double max_mag = 1e-6;
                for (double m : mags) max_mag = std::max(max_mag, m);
                double bar_w = (gw - 16.0) / BINS;
                for (int i = 0; i < BINS; ++i) {
                    double norm = std::clamp(mags[i] / max_mag, 0.0, 1.0);
                    double bh = (gh - 24.0) * norm;
                    double bx = gx + 8.0 + bar_w * i;
                    double by = gy + gh - 8.0 - bh;
                    uint8_t r = static_cast<uint8_t>(120 + 80 * norm);
                    uint8_t g = static_cast<uint8_t>(180 + 75 * norm);
                    ctx.batch.draw_rect(bx, by, std::max(1.0, bar_w - 1.0), bh, r, g, 255, 210);
                }
            } else {
                debug_text(gx + kPanelPad, gy + kRow + 8.0, "PCM tap unavailable", 255, 170, 170, 225);
            }
            left_y += gh + 8.0;
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
                              "L%d xy=(%.0f,%.0f) rot=%.1fdeg a=%.2f s=%.2f sx=%.1f",
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
                              "L%d xy=(%.0f,%.0f) rot=%.1fdeg a=%.2f s=%.2f sx=%.1f",
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
                std::snprintf(buf, sizeof(buf), "L%d a=%.2f s=%.2f r=%.1fdeg sx=%.1f sy=%.1f",
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

            if (has_debug(DebugFlag::MH_TEXTURE_STATUS) && ns.mh) {
                const bool mh_tex = note_uses_mh_texture(ns);
                const char* mode = mh_tex ? "mh-tex" : "base-tex";
                uint8_t r = mh_tex ? 255 : 255;
                uint8_t g = mh_tex ? 140 : 220;
                uint8_t b = mh_tex ? 255 : 140;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "MH %s %s", note_kind_name(ns.kind), mode);
                debug_text(x + 10.0, y - 30.0, buf, r, g, b, 235);
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

        // ── SCORE_BREAKDOWN: detailed P/G/B/M counts, accuracy, score ────────
        if (has_debug(DebugFlag::SCORE_BREAKDOWN)) {
            // Recount from NoteState for accuracy
            int cp = 0, cg = 0, cb = 0, cm = 0;
            for (const auto& ns : states) {
                if (!ns.judged) continue;
                if (ns.judge_grade == "PERFECT") ++cp;
                else if (ns.judge_grade == "GOOD") ++cg;
                else if (ns.judge_grade == "BAD") ++cb;
                else if (ns.miss) ++cm;
            }
            debug_cnt_perfect = cp; debug_cnt_good = cg;
            debug_cnt_bad = cb; debug_cnt_miss = cm;

            double acc = (judge.judged_cnt > 0)
                ? (judge.acc_sum / judge.judged_cnt) * 100.0 : 100.0;
            char l1[128], l2[128], l3[128];
            std::snprintf(l1, sizeof(l1), "P=%d G=%d B=%d M=%d",
                          cp, cg, cb, cm);
            std::snprintf(l2, sizeof(l2), "combo=%d/%d  judged=%d/%d",
                          judge.combo, judge.max_combo,
                          judge.judged_cnt, playable_notes);
            std::snprintf(l3, sizeof(l3), "acc=%.2f%%  score=%d",
                          acc, fr.hud.score);
            double tw1 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, l1);
            double tw2 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, l2);
            double tw3 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, l3);
            double tw = std::max({tw1, tw2, tw3});
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            double panel_h = kRow * 3.0 + 8.0;
            draw_panel(px, right_y - 4, pw, panel_h);
            debug_text(px + kPanelPad, right_y,            l1, 255, 230, 180, 235);
            debug_text(px + kPanelPad, right_y + kRow,     l2, 200, 230, 255, 225);
            debug_text(px + kPanelPad, right_y + kRow * 2, l3, 200, 255, 200, 225);
            right_y += panel_h + 4.0;
        }

        // ── CHART_METADATA: chart info panel ─────────────────────────────────
        if (has_debug(DebugFlag::CHART_METADATA)) {
            int n_tap = 0, n_drag = 0, n_hold = 0, n_flick = 0, n_fake = 0;
            for (const auto& n : chart.notes) {
                if (n.fake) { ++n_fake; continue; }
                switch (n.kind) {
                case 1: ++n_tap;   break;
                case 2: ++n_drag;  break;
                case 3: ++n_hold;  break;
                case 4: ++n_flick; break;
                default: break;
                }
            }
            char m1[160], m2[160], m3[128];
            std::snprintf(m1, sizeof(m1), "lines=%zu  notes=%d  fake=%d",
                          chart.lines.size(), playable_notes, n_fake);
            std::snprintf(m2, sizeof(m2), "T=%d D=%d H=%d F=%d",
                          n_tap, n_drag, n_hold, n_flick);
            std::snprintf(m3, sizeof(m3), "offset=%.3fs  duration=%.2fs",
                          chart.offset, chart_end);
            double tw1 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, m1);
            double tw2 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, m2);
            double tw3 = ctx.hud_ren.text_width(ctx.hud_ren.font_small, m3);
            double tw = std::max({tw1, tw2, tw3});
            double pw = tw + kPanelPad * 2.0 + 4.0;
            double px = right_panel_x(pw);
            double panel_h = kRow * 3.0 + 8.0;
            draw_panel(px, right_y - 4, pw, panel_h);
            debug_text(px + kPanelPad, right_y,            m1, 220, 220, 255, 235);
            debug_text(px + kPanelPad, right_y + kRow,     m2, 220, 240, 200, 225);
            debug_text(px + kPanelPad, right_y + kRow * 2, m3, 200, 220, 240, 225);
            right_y += panel_h + 4.0;
        }

        // ── JUDGMENT_HISTORY: recent judgment feed ───────────────────────────
        if (has_debug(DebugFlag::JUDGMENT_HISTORY) && !debug_judge_history.empty()) {
            int max_entries = std::min<int>(8, static_cast<int>(debug_judge_history.size()));
            double panel_h = kRow * max_entries + 8.0;
            double pw = 180.0;
            double px = right_panel_x(pw);
            draw_panel(px, right_y - 4, pw, panel_h);
            for (int i = 0; i < max_entries; ++i) {
                const auto& e = debug_judge_history[i];
                double fade = std::max(0.0, 1.0 - e.age / 3.0);
                auto a = static_cast<uint8_t>(230 * fade);
                uint8_t r = 220, g = 220, b = 220;
                if (e.text.find("PERFECT") != std::string::npos) { r = 255; g = 255; b = 180; }
                else if (e.text.find("GOOD") != std::string::npos) { r = 180; g = 255; b = 200; }
                else if (e.text.find("BAD") != std::string::npos) { r = 255; g = 200; b = 150; }
                else if (e.text.find("MISS") != std::string::npos) { r = 255; g = 140; b = 140; }
                debug_text(px + kPanelPad, right_y + kRow * i, e.text, r, g, b, a);
            }
            right_y += panel_h + 4.0;
        }

        // ── HOLD_STATE: highlight active holds ───────────────────────────────
        if (has_debug(DebugFlag::HOLD_STATE)) {
            for (const auto& ns_snap : fr.notes) {
                if (!ns_snap.is_hold) continue;
                double x = ns_snap.wx, y = ns_snap.wy;
                render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
                const Note* note = note_lookup(ns_snap.nid);
                if (!note) continue;

                // Find NoteState for this note
                const NoteState* ns_state = nullptr;
                for (const auto& s : states) {
                    if (s.note && s.note->nid == ns_snap.nid) { ns_state = &s; break; }
                }
                if (!ns_state) continue;

                double hold_dur = note->t_end - note->t_hit;
                if (hold_dur <= 0.0) continue;
                double prog = std::clamp((fr.t - note->t_hit) / hold_dur, 0.0, 1.0);

                // Draw progress bar above note
                double bar_w = 40.0, bar_h = 4.0;
                double bar_x = x - bar_w * 0.5;
                double bar_y = y - 20.0;
                ctx.batch.draw_rect(bar_x, bar_y, bar_w, bar_h, 60, 60, 60, 180);
                if (ns_state->holding) {
                    ctx.batch.draw_rect(bar_x, bar_y, bar_w * prog, bar_h, 120, 255, 180, 220);
                } else if (ns_state->released_early) {
                    ctx.batch.draw_rect(bar_x, bar_y, bar_w * prog, bar_h, 255, 120, 120, 220);
                    debug_text(x + 24.0, bar_y - 2.0, "EARLY", 255, 120, 120, 220);
                } else if (ns_state->hold_finalized) {
                    ctx.batch.draw_rect(bar_x, bar_y, bar_w, bar_h, 180, 255, 180, 220);
                }
            }
        }

        // ── MISS_INDICATOR: flash on recently missed notes ───────────────────
        if (has_debug(DebugFlag::MISS_INDICATOR)) {
            for (auto& [nid, remain] : debug_miss_flash) {
                if (remain <= 0.0) continue;
                auto it = debug_note_by_id.find(nid);
                if (it == debug_note_by_id.end()) continue;
                // Try to find the note in the current frame snapshot
                for (const auto& ns : fr.notes) {
                    if (ns.nid != nid) continue;
                    double x = ns.wx, y = ns.wy;
                    render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
                    auto a = static_cast<uint8_t>(200 * std::min(1.0, remain / 0.3));
                    double sz = 20.0 + 10.0 * (1.0 - remain / 0.5);
                    draw_box_outline(x - sz, y - sz, sz * 2.0, sz * 2.0,
                                     255, 80, 80, a);
                    debug_text(x + sz + 4.0, y - 6.0, "MISS", 255, 80, 80, a);
                    break;
                }
            }
        }

        // ── LINE_ALPHA_BAR: alpha value bar beside each judge line ───────────
        if (has_debug(DebugFlag::LINE_ALPHA_BAR)) {
            for (const auto& ls : fr.lines) {
                double cx = ls.x, cy = ls.y;
                render::apply_expand_xy(cx, cy, W, H, cfg.expand_factor);
                double bar_w = 30.0, bar_h = 4.0;
                double bx = cx - bar_w * 0.5;
                double by = cy + 12.0;
                // Background
                ctx.batch.draw_rect(bx, by, bar_w, bar_h, 60, 60, 60, 160);
                // Fill proportional to alpha
                double fill = bar_w * std::clamp(ls.alpha01, 0.0, 1.0);
                uint8_t r = static_cast<uint8_t>(120 + 135 * ls.alpha01);
                ctx.batch.draw_rect(bx, by, fill, bar_h, r, 200, 160, 200);
            }
        }

        // ── NOTE_DENSITY_GRAPH: NPS over upcoming time window ────────────────
        if (has_debug(DebugFlag::NOTE_DENSITY_GRAPH)) {
            const double gw = 180.0, gh = 56.0;
            const double gx = 12.0;
            const double gy = std::max(left_y + 4.0, static_cast<double>(H) - gh - 24.0);
            draw_panel(gx, gy, gw, gh);

            constexpr int BINS = 20;
            constexpr double WINDOW = 4.0; // seconds ahead
            int counts[BINS] = {};
            double bin_dur = WINDOW / BINS;
            for (const auto& n : chart.notes) {
                if (n.fake) continue;
                double dt = n.t_hit - fr.t;
                if (dt < 0.0 || dt >= WINDOW) continue;
                int b = std::min(BINS - 1, static_cast<int>(dt / bin_dur));
                ++counts[b];
            }
            int max_c = 1;
            for (int i = 0; i < BINS; ++i) max_c = std::max(max_c, counts[i]);

            double bar_w = (gw - 16.0) / BINS;
            for (int i = 0; i < BINS; ++i) {
                double bh = (gh - 24.0) * counts[i] / static_cast<double>(max_c);
                double bx = gx + 8.0 + bar_w * i;
                double by = gy + gh - 8.0 - bh;
                uint8_t g = static_cast<uint8_t>(180 + 75 * counts[i] / static_cast<double>(max_c));
                ctx.batch.draw_rect(bx, by, bar_w - 1.0, bh, 100, g, 200, 200);
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "density (%.0fs window)", WINDOW);
            debug_text(gx + 8.0, gy + 2.0, buf, 200, 220, 255, 220);
            left_y = gy + gh + 4.0;
        }

        // ── SCROLL_SPEED_OVERLAY: scroll speed text beside each line ─────────
        if (has_debug(DebugFlag::SCROLL_SPEED_OVERLAY)) {
            for (const auto& ls : fr.lines) {
                double cx = ls.x, cy = ls.y;
                render::apply_expand_xy(cx, cy, W, H, cfg.expand_factor);
                char buf[48];
                std::snprintf(buf, sizeof(buf), "s=%.2f", ls.scroll);
                debug_text(cx - 40.0, cy + 4.0, buf, 180, 220, 255, 200);
            }
        }

        // ── EXPAND_BORDER: optionally annotate the always-on original viewport ─
        if (has_debug(DebugFlag::EXPAND_BORDER) && cfg.expand_factor > 1.000001) {
            double ef = cfg.expand_factor;
            double cx = W * 0.5, cy = H * 0.5;
            double hw = cx / ef, hh = cy / ef;
            double bx = cx - hw, by = cy - hh;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "expand=%.2f", ef);
            debug_text(bx + 4.0, by - 16.0, buf, 255, 180, 60, 200);
        }

        // ── CENTER_CROSSHAIR: screen center marker ───────────────────────────
        if (has_debug(DebugFlag::CENTER_CROSSHAIR)) {
            double cx = W * 0.5, cy = H * 0.5;
            double sz = 20.0;
            ctx.batch.draw_line(cx - sz, cy, cx + sz, cy, 1.0, 200, 200, 200, 140);
            ctx.batch.draw_line(cx, cy - sz, cx, cy + sz, 1.0, 200, 200, 200, 140);
            // Small center dot
            ctx.batch.draw_rect(cx - 2.0, cy - 2.0, 4.0, 4.0, 255, 255, 255, 160);
        }

        // ── SIMULTANEOUS_INDICATOR: highlight multi-hit notes ────────────────
        if (has_debug(DebugFlag::SIMULTANEOUS_INDICATOR)) {
            for (const auto& ns : fr.notes) {
                if (!ns.mh) continue;
                double x = ns.wx, y = ns.wy;
                render::apply_expand_xy(x, y, W, H, cfg.expand_factor);
                double sz = base_note_w * ns.size_px * 0.6;
                // Draw diamond shape
                ctx.batch.draw_line(x, y - sz, x + sz, y, 1.0, 255, 200, 255, 200);
                ctx.batch.draw_line(x + sz, y, x, y + sz, 1.0, 255, 200, 255, 200);
                ctx.batch.draw_line(x, y + sz, x - sz, y, 1.0, 255, 200, 255, 200);
                ctx.batch.draw_line(x - sz, y, x, y - sz, 1.0, 255, 200, 255, 200);
            }
        }

        // ── NOTE_APPROACH_GUIDE: lines from notes to their judge line ────────
        if (has_debug(DebugFlag::NOTE_APPROACH_GUIDE)) {
            for (const auto& ns : fr.notes) {
                if (ns.judged) continue;
                double nx = ns.wx, ny = ns.wy;
                render::apply_expand_xy(nx, ny, W, H, cfg.expand_factor);
                const Note* note = note_lookup(ns.nid);
                if (!note) continue;
                // Find the parent line in the snapshot
                for (const auto& ls : fr.lines) {
                    if (ls.lid != note->line_id) continue;
                    double lx = ls.x, ly = ls.y;
                    render::apply_expand_xy(lx, ly, W, H, cfg.expand_factor);
                    double dist = std::sqrt((nx - lx) * (nx - lx) + (ny - ly) * (ny - ly));
                    if (dist < 4.0) break; // too close, skip
                    auto a = static_cast<uint8_t>(std::min(180.0, 40.0 + 140.0 * (1.0 - dist / static_cast<double>(H))));
                    ctx.batch.draw_line(nx, ny, lx, ly, 1.0, 160, 200, 255, a);
                    break;
                }
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

        // Update MISS_INDICATOR flash timers
        if (has_debug(DebugFlag::MISS_INDICATOR)) {
            for (const auto& s : states) {
                if (s.miss && s.note) {
                    debug_miss_flash.try_emplace(s.note->nid, 0.5); // 0.5s flash
                }
            }
            for (auto it = debug_miss_flash.begin(); it != debug_miss_flash.end(); ) {
                it->second -= dt_frame;
                if (it->second <= 0.0)
                    it = debug_miss_flash.erase(it);
                else
                    ++it;
            }
        }

        // Update JUDGMENT_HISTORY feed
        if (has_debug(DebugFlag::JUDGMENT_HISTORY)) {
            for (const auto& s : states) {
                if (!s.judged || s.judge_grade.empty() || !s.note) continue;
                int nid = s.note->nid;
                // Check if this note is already recorded (by nid)
                bool found = false;
                for (const auto& e : debug_judge_history) {
                    if (e.nid == nid) { found = true; break; }
                }
                if (!found) {
                    char buf[80];
                    std::snprintf(buf, sizeof(buf), "N%d %s dt=%+.1fms",
                                  nid, s.judge_grade.c_str(), s.judge_delta_ms);
                    debug_judge_history.push_front({nid, buf, 0.0});
                    while (debug_judge_history.size() > 20) debug_judge_history.pop_back();
                }
            }
            // Age all entries
            for (auto& e : debug_judge_history) e.age += dt_frame;
            // Remove entries older than 4 seconds
            while (!debug_judge_history.empty() && debug_judge_history.back().age > 4.0)
                debug_judge_history.pop_back();
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
        if (cfg.expand_factor > 1.000001) {
            double ef = cfg.expand_factor;
            double cx = W * 0.5, cy = H * 0.5;
            double hw = cx / ef, hh = cy / ef;
            double bx = cx - hw, by = cy - hh;
            double bw = hw * 2.0, bh = hh * 2.0;
            ctx.batch.draw_line(bx, by, bx + bw, by, 1.0, 255, 180, 60, 180);
            ctx.batch.draw_line(bx, by + bh, bx + bw, by + bh, 1.0, 255, 180, 60, 180);
            ctx.batch.draw_line(bx, by, bx, by + bh, 1.0, 255, 180, 60, 180);
            ctx.batch.draw_line(bx + bw, by, bx + bw, by + bh, 1.0, 255, 180, 60, 180);
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
        PHLOG_INFO(Engine, "Restarting chart at t=" << t
            << " judged=" << judge.judged_cnt
            << " combo=" << judge.max_combo);
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

    void stop_recording_session() {
        const bool was_recording = is_recording;
        if (recorder.is_active()) recorder.finish();
        is_recording = false;
        if (args.record_profile && was_recording && !record_profile_reported) {
            record_prof.final_report(recorder);
            record_profile_reported = true;
        }
    }

    void do_capture() {
        last_capture_readback_api_ms_ = 0.0;
        last_capture_readback_convert_ms_ = 0.0;
        last_capture_readback_copy_ms_ = 0.0;
        last_capture_queue_ms_ = 0.0;
        if (args.record_start > -0.5 && t < args.record_start) {
            PHLOG_TRACE(Record, "Capture waiting for start: t=" << t
                << " start=" << args.record_start);
            return;
        }
        if (args.record_end > 0.0 && t > args.record_end) {
            PHLOG_INFO(Record, "Capture reached end: t=" << t
                << " end=" << args.record_end);
            stop_recording_session();
            return;
        }
        sdl::ReadbackTiming readback_timing{};
        if (!ctx.window.read_pixels_rgba(readback_rgba.data(),
                                         args.record_profile ? &readback_timing : nullptr)) {
            PHLOG_ERROR(Record, "Readback failed, stopping recorder");
            stop_recording_session();
            return;
        }
        uint64_t q0 = args.record_profile ? record_prof.now() : 0;
        if (!recorder.capture_rgba(readback_rgba.data(), W, H, t)) {
            PHLOG_ERROR(Record, "Capture failed, stopping recorder");
            stop_recording_session();
            return;
        }
        uint64_t q1 = args.record_profile ? record_prof.now() : 0;
        if (args.record_profile) {
            last_capture_readback_api_ms_ = readback_timing.api_ms;
            last_capture_readback_convert_ms_ = readback_timing.convert_ms;
            last_capture_readback_copy_ms_ = readback_timing.copy_ms;
            last_capture_queue_ms_ = record_prof.to_ms(q1 - q0);
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

    double last_capture_readback_api_ms_ = 0.0;
    double last_capture_readback_convert_ms_ = 0.0;
    double last_capture_readback_copy_ms_ = 0.0;
    double last_capture_queue_ms_ = 0.0;
};

} // namespace phigros::app
